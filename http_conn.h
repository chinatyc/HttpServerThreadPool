#ifndef HTTPCONNECTION_H
#define HTTPCOMMECTION_H

#include <unistd.h>
#include <signal.h>
//#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include "locker.h"

class Http_conn{
public:
  static const int FILENAME_LEM=200;
  static const int READ_BUFFER_SIZE=2048;
  static const int WRITE_BUFFER_SIZE=1024;

  enum CHECK_STATE {CHECK_STATE_REQUESTLINE=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};
  enum METHOD {GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,PATCH};//若干种http请求方式，但是通常只实现其中几种
  enum HTTP_CODE {NO_REQUEST,GET_REQUEST,BAD_REQUEST,FORBIDDEN_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION,NO_RESOURCE,FILE_REQUEST};
  enum LINE_STATUS {LINE_OK=0,LINE_BAD,LINE_OPEN};

  Http_conn(){}
  ~Http_conn(){}

  void init(int sockfd,const sockaddr_in& addr);//疑问，为什么要用&
  void close_conn(bool real_close=true);
  void process();//处理客户请求
  bool read();//非阻塞读
  bool write();//非阻塞写操

private:
  void init();
  HTTP_CODE process_read();//解析http请求
  bool process_write(HTTP_CODE ret);//输出应答

  HTTP_CODE parse_request_line(char* text);
  HTTP_CODE parse_headers(char* text);
  HTTP_CODE parse_content(char* text);
  HTTP_CODE do_request();//???
  char* get_line(){return m_read_buf+m_start_line;}
  LINE_STATUS parse_line();

  //下面的一组函数被process_write调用；
  void unamp();
  bool add_response(const char* format,...);
  bool add_content(const char* content);
  bool add_status_line(int status,const char* title);
  bool add_content_length(int content_length);
  bool add_linger();
  bool add_blank_line();
  bool add_headers(int content_len);

public:
  //所有socket上的时间都在被注册到同一个epoll内核时间表中，所以static
  static int m_epollfd;
  static int m_user_count;

private:
  int m_sockfd;
  sockaddr_in m_address;

  char m_read_buf[READ_BUFFER_SIZE];
  int m_read_idx;//缓冲区内已经读入客户数据的最后一个字节的下一个字节
  int m_checked_idx;//当前正在分析的缓冲区的位置
  int m_start_line;//起始行位置
  char m_write_buf[WRITE_BUFFER_SIZE];
  int m_write_idx;

  CHECK_STATE m_check_state;
  METHOD m_method;

  char m_real_file[FILENAME_LEM];//客户端请求的完整实际文件路径；
  char* m_url; //客户端请求文件名
  char* m_version;//http版本号
  char* m_host;//主机名；ou
  int m_content_length;//http请求的消息长度
  bool m_linger;
  char* m_file_address;//客户请求的目标文件被mmap到内存的起始位置？？
  struct stat m_file_stat;//目标文件的状态
  struct iovec m_iv[2]; //提供给writev使用;
  int m_iv_count; //被写内存块的数量


};
#endif
