#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ThreadPool.h"
#include "http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern void addfd(int epollfd,int fd, bool one_shot);
extern int removefd(int epollfd,int fd);

//添加信号处理
void addsig(int sig,void(handler)(int),bool restart=true){
  struct sigaction sa;
  memset(&sa,'\0',sizeof(sa));
  sa.sa_handler=handler;
  if(restart){
    sa.sa_flags|=SA_RESTART;
  }
  sigfillset(&sa.sa_mask);
  assert(sigaction(sig,&sa,NULL)!=-1);
}

void show_error(int connfd,const char* info){
  printf("%s",info);
  send(connfd,info,strlen(info),0);
  close(connfd);
}

int main(int argc,char* argv[]){
  if(argc<=2){
    printf("error argc \n");
    return 1;
  }
  const char* ip=argv[1];
  int port=atoi(argv[2]);

  addsig(SIGPIPE,SIG_IGN);//忽略SIGPIPE信号；
  threadpool<Http_conn>* pool=NULL;
  try{
    pool=new threadpool<Http_conn>;
  }catch(...){
    return 1;
  }

  Http_conn* users=new Http_conn[MAX_FD];//预先为每一个可能的客户连接分配一个http_conn对象；
  assert(users);
  int user_count=0;

  int listenfd=socket(PF_INET,SOCK_STREAM,0);
  assert(listenfd>0);

  //close()立刻返回，但不会发送未发送完成的数据，而是通过一个REST包强制的关闭socket描述符，即强制退出。
  struct linger tmp={1,0};
  setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

  int ret=0;
  struct sockaddr_in address;
  bzero(&address,sizeof(address));
  address.sin_family = AF_INET;
  inet_pton(AF_INET,ip,&address.sin_addr);
  address.sin_port=htons(port);

  ret=bind(listenfd,(struct sockaddr*)&address,sizeof(address));
//  printf("listenfd %d\n",ret);
  assert(ret>=0);

  ret=listen(listenfd,5);
  assert(ret>=0);

  epoll_event events[MAX_EVENT_NUMBER];
  int epollfd=epoll_create(5);
  assert(epollfd>0);
  addfd(epollfd,listenfd,false);
  Http_conn::m_epollfd=epollfd;

  while(true){
    int number=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
    if((number<0)&&(errno!=EINTR)){
      printf("epoll_wait_error");
      break;
    }

    for(int i=0;i<number;i++){
      int sockfd=events[i].data.fd;
      if(sockfd==listenfd){//新连接进来
        struct sockaddr_in client_address;
        socklen_t client_addrlength=sizeof(client_address);
        int connfd=accept(listenfd,(struct sockaddr*)&client_address,&client_addrlength);
        if(connfd<0){
          printf("accept error");
          continue;
        }
        if(Http_conn::m_user_count>=MAX_FD){
          show_error(connfd,"Too many Connecton");
          continue;
        }
        users[connfd].init(connfd,client_address);
      }else if(events[i].events & (EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
        //发生异常
        users[sockfd].close_conn();
      }else if(events[i].events & EPOLLIN){
        //有数据进来；
        if(users[sockfd].read()){
          pool->append(users+sockfd);//把该Http_conn对象加入到任务队列
        }else{
          users[sockfd].close_conn();
        }
      }else if(events[i].events & EPOLLOUT){
        if(!users[sockfd].write()){
          users[sockfd].close_conn();
        }
      }
    }
  }
  close(epollfd);
  close(listenfd);
  delete[] users;
  delete pool;
  return 0;
}
