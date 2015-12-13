#include "http_conn.h"

//定义一些http相应状态信息
const char* ok_200_title="OK";
const char* error_400_title="Bad Request";
const char* error_400_form="Your request has bad impossible to satisfy.\n";
const char* error_500_title="500 title";
const char* error_500_form="500 content";
const char* error_404_title="404 title";
const char* error_404_form="404 content";
const char* error_403_form="403 form";
const char* error_403_title="403 title";

const char* doc_root="/usr/philotian/philoweb";//网站根目录；

int setnonblock(int fd){
  int old_option=fcntl(fd,F_GETFL);
  int new_option=old_option|O_NONBLOCK;
  fcntl(fd,F_SETFL,new_option);
  return old_option;
}

void addfd(int epollfd,int fd,bool one_shot){
  epoll_event event;
  event.data.fd=fd;
  event.events=EPOLLIN|EPOLLET|EPOLLRDHUP;
  if(one_shot){
    event.events|=EPOLLONESHOT;
  }
  epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
  setnonblock(fd);
}

void removefd(int epollfd,int fd){
  epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
  close(fd);
}

void modfd(int epollfd,int fd,int ev){
  epoll_event event;
  event.data.fd=fd;
  event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
  epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int Http_conn::m_user_count=0;
int Http_conn::m_epollfd=-1;

void Http_conn::close_conn(bool real_close){
  if(m_sockfd!=-1){
    removefd(m_epollfd,m_sockfd);
    m_sockfd=-1;
    m_user_count--;
  }
}

void Http_conn::init(int sockfd, const sockaddr_in& addr){
  m_sockfd=sockfd;
  m_address=addr;
  //下面两行可以避免TIME_WAIT，仅用于测试
  int reuse=1;
  setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

  addfd(m_epollfd,sockfd,true);
  m_user_count++;
  init();
}

void Http_conn::init(){
  m_check_state=CHECK_STATE_REQUESTLINE;
  m_linger=false;
  m_method=GET;
  m_url=0;
  m_version=0;
  m_content_length=0;
  m_host=0;
  m_start_line=0;
  m_checked_idx=0;
  m_read_idx=0;
  m_write_idx=0;
  memset(m_read_buf,'\0',READ_BUFFER_SIZE);
  memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
  memset(m_real_file,'\0',FILENAME_LEM);
}

//解析一行
Http_conn::LINE_STATUS Http_conn::parse_line(){
  char temp;
  for(;m_checked_idx<m_read_idx;m_checked_idx++){
    temp=m_read_buf[m_checked_idx];
    if(temp=='\r'){
      //如果"\r"碰巧是目前buffer中最后一个已经被读入的客户端，那么表明这次分析并没有读入一个完整的行，返回LINE_OPEN表示继续读数据；
      if((m_checked_idx+1)==m_read_idx){
        return LINE_OPEN;
      }
      else if (m_read_buf[m_checked_idx+1]=='\n'){
        m_read_buf[m_checked_idx++]='\0';
        m_read_buf[m_checked_idx++]='\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }else if(temp=='\n'){//如果读取到的是一个\n，那么表明也可能是一个完整行
      if((m_checked_idx>1)&&(m_read_buf[m_checked_idx-1]=='\r')){
        m_read_buf[m_checked_idx++]='\0';
        m_read_buf[m_checked_idx++]='\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }
}

bool Http_conn::read(){
  //缓冲区满
  if(m_read_idx>=READ_BUFFER_SIZE){
    return false;
  }
  int byte_read=0;
  while(true){
    byte_read=recv(m_sockfd,m_read_buf+m_read_idx, READ_BUFFER_SIZE - m_read_idx,0);
    if(byte_read==-1){
      if(errno==EAGAIN||errno==EWOULDBLOCK){
        break;
      }
      return true;//非阻塞，缓冲区繁忙，稍后再试
    }else if(byte_read==0){
      return false;//客户端关闭
    }
    m_read_idx+=byte_read;
  }
  return true;
}

Http_conn::HTTP_CODE Http_conn::parse_request_line(char* text){
  m_url=strpbrk(text," \t");
  if(!m_url){
    return BAD_REQUEST;
  }
  *m_url++='\0';

  char* method=text;
  if(strcasecmp(method,"GET")==0){//暂时只处理GET请求
    m_method=GET;
  }else{
    return BAD_REQUEST;
  }

  m_url+=strspn(m_url," \t");
  m_version=strpbrk(m_url," \t");
  if(!m_version){
    return BAD_REQUEST;
  }
  *m_version++='\0';
  m_version+=strspn(m_version," \t");
  if(strcasecmp(m_version,"HTTP/1.1")!=0){//只支持http1.1
    return BAD_REQUEST;
  }
  if(strncasecmp(m_url,"http://",7)==0){
    m_url+=7;
    m_url=strchr(m_url,'/');
  }
  if(!m_url||m_url[0]!='/'){
    return BAD_REQUEST;
  }
  m_check_state=CHECK_STATE_HEADER;
  return NO_REQUEST;
}

//解析一行头部信息；
Http_conn::HTTP_CODE Http_conn::parse_headers(char* text){
  if(text[0]=='\0'){
    //如果HTTP有消息体的话，还要将状态机转移到CHECK_STATE_CONTENT
    if (m_content_length!=0) {
      m_check_state=CHECK_STATE_CONTENT;
      return NO_REQUEST;
    }
    return GET_REQUEST;
  }else if(strncasecmp(text,"Connection:",11)==0){
    text+=11;
    text+=strspn(text," \t");
    if(strcasecmp(text,"keep-alive")==0){
      m_linger=true;
    }
  }else if(strncasecmp(text,"Content-Length:",15)==0){
    text+=15;
    text+=strspn(text," \t");
    m_content_length=atoi(text);
  }else if(strncasecmp(text,"Host:",5)==0){
    text+=5;
    text+=strspn(text," \t");
    m_host=text;
  }else{
    printf("unknow header %s\n", text);
  }
  return NO_REQUEST;
}

Http_conn::HTTP_CODE Http_conn::parse_content(char* text){
  if(m_read_idx>=(m_content_length+m_checked_idx)){
    text[m_content_length]='\0';
    return GET_REQUEST;
  }
  return NO_REQUEST;
}

//解析入口函数
Http_conn::HTTP_CODE Http_conn::process_read(){
  LINE_STATUS line_status=LINE_OK;
  HTTP_CODE ret=NO_REQUEST;
  char* text=0;
  while(((m_check_state==CHECK_STATE_CONTENT)&&(line_status==LINE_OK))||((line_status=parse_line())==LINE_OK)){
    text=get_line();//有疑问？
    m_start_line=m_checked_idx;

    //状态转移
    switch(m_check_state){
      case CHECK_STATE_REQUESTLINE:
      {
        ret=parse_request_line(text);
        if (ret==BAD_REQUEST){
          return BAD_REQUEST;
        }
        break;
      }
      case CHECK_STATE_HEADER:
      {
        ret=parse_headers(text);
        if(ret==BAD_REQUEST){
          return BAD_REQUEST;
        }else if(ret==GET_REQUEST){
          return do_request();
        }
        break;
      }
      case CHECK_STATE_CONTENT:
      {
        ret=parse_content(text);
        if(ret==GET_REQUEST){
          return do_request();
        }
        line_status=LINE_OPEN;
        break;
      }
      default:
      {
        return INTERNAL_ERROR;
      }
    }
  }
  return NO_REQUEST;
}

//获取到完整http请求之后，根据请求文件的属性判断玩家访问的返回结果
Http_conn::HTTP_CODE Http_conn::do_request(){
  strcpy(m_real_file,doc_root);
  int len=strlen(doc_root);
  strncpy(m_real_file+len,m_url,FILENAME_LEM-len-1);
  if(stat(m_real_file,&m_file_stat)<0){
    return NO_RESOURCE;
  }
  if(!(m_file_stat.st_mode&S_IROTH)){
    return FORBIDDEN_REQUEST;
  }
  if(S_ISDIR(m_file_stat.st_mode)){
    return BAD_REQUEST;
  }
  int fd=open(m_real_file,O_RDONLY);
  m_file_address=(char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);//把文件映射到内存当中
  close(fd);
  return FILE_REQUEST;
}

void Http_conn::unamp(){
  if (m_file_address) {
    munmap(m_file_address,m_file_stat.st_size);
    m_file_address=0;
  }
}


bool Http_conn::write(){
  int temp=0;
  int bytes_have_send=0;
  int bytes_to_send=m_write_idx;
  if(bytes_to_send==0){
    modfd(m_epollfd,m_sockfd,EPOLLIN);
    init();
    return true;
  }
  while(1){
    temp=writev(m_sockfd,m_iv,m_iv_count);
    if(temp<=-1){
      if(errno==EAGAIN){
        modfd(m_epollfd,m_sockfd,EPOLLOUT);
        return true;
      }
      unamp();
      return false;
    }

    bytes_to_send-=temp;
    bytes_have_send+=temp;
    if(bytes_to_send<=bytes_have_send){
      unamp();
      if(m_linger){
        init();
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return true;
      }else{
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return false;
      }
    }
  }
}

bool Http_conn::process_write(HTTP_CODE ret){
  switch (ret) {
    case INTERNAL_ERROR:
    {
      add_status_line(500,error_500_title);
      add_headers(strlen(error_500_form));
      add_content(error_500_form);
      break;
    }
    case BAD_REQUEST:
    {
      add_status_line(400,error_400_title);
      add_headers(strlen(error_400_form));
      add_content(error_400_form);
      break;
    }
    case NO_RESOURCE:
    {
      add_status_line(404,error_404_title);
      add_headers(strlen(error_404_form));
      add_content(error_404_form);
      break;
    }
    case FORBIDDEN_REQUEST:
    {
      add_status_line(403,error_403_title);
      add_headers(strlen(error_403_form));
      add_content(error_403_form);
      break;
    }
    case FILE_REQUEST:
    {
      add_status_line(200,ok_200_title);
      if(m_file_stat.st_size!=0){
        add_headers(m_file_stat.st_size);
        m_iv[0].iov_base=m_write_buf;
        m_iv[0].iov_len=m_write_idx;
        m_iv[1].iov_base=m_file_address;
        m_iv[1].iov_len=m_file_stat.st_size;
        m_iv_count=2;
        return true;
      }else{
        const char* ok_string="<html><body></body></html>";
        add_headers(strlen(ok_string));
        add_content(ok_string);
      }
      break;
    }
    default:{
      return false;
    }
  }
  m_iv[0].iov_base=m_write_buf;
  m_iv[0].iov_len=m_write_idx;
  m_iv_count=1;
  return true;
}

void Http_conn::process(){
  HTTP_CODE read_ret=process_read();
  if(read_ret==NO_REQUEST){
    modfd(m_epollfd,m_sockfd,EPOLLIN);
    return;
  }
  bool write_ret=process_write(read_ret);
  if(!write_ret){
    close_conn();
  }
  modfd(m_epollfd,m_sockfd,EPOLLOUT);
}

//往写缓冲区添加数据
bool Http_conn::add_response(const char* format,...){
  if(m_write_idx>=WRITE_BUFFER_SIZE){
    return false;
  }
  va_list arg_list;
  va_start(arg_list,format);
  int len=vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-1-m_write_idx,format,arg_list);
  if(len>=(WRITE_BUFFER_SIZE - 1 - m_write_idx)){
    return false;
  }
  m_write_idx+=len;
  va_end(arg_list);
  return true;
}

bool Http_conn::add_status_line(int status, const char* title){
  return add_response("%s %d %s \r\n","HTTP/1.1",status,title);
}

bool Http_conn::add_content_length(int content_len){
  return add_response("Content-Length: %d\r\n",content_len);
}

bool Http_conn::add_linger(){
  return add_response("Connection: %s\r\n",(m_linger==true)?"keep-alive":"close");
}

bool Http_conn::add_blank_line(){
  return add_response("%s","\r\n");
}

bool Http_conn::add_content(const char* content){
  return add_response("%s",content);
}

bool Http_conn::add_headers(int content_len){
  add_content_length(content_len);
  add_linger();
  add_blank_line();
}
