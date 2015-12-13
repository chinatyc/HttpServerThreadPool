#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>

//信号量
class Sem{
private:
	sem_t m_sem;
public:
	Sem(){
		if(sem_init(&m_sem,0,0)!=0){
			throw std::exception();
		}
	}
	~Sem(){
		sem_destroy(&m_sem);
	}

	bool wait(){
		printf("wait !!!!\n");
		return sem_wait(&m_sem)==0;//阻塞版本
	}

	bool post(){
		return sem_post(&m_sem)==0;
	}
};

//互斥锁
class Locker
{
private:
	pthread_mutex_t m_mutex;
public:
	Locker(){
		if(pthread_mutex_init(&m_mutex,NULL)!=0){
			throw std::exception();
		}
	}
	~Locker(){
		pthread_mutex_destroy(&m_mutex);
	}

	bool lock(){
		return pthread_mutex_lock(&m_mutex)==0;
	}

	bool unlock(){
		return pthread_mutex_unlock(&m_mutex)==0;
	}
};

//条件变量
class Cond{
private:
	pthread_mutex_t m_mutex;
	pthread_cond_t m_cond;
public:
	Cond(){
		if(pthread_mutex_init(&m_mutex,NULL)!=0){
			throw std::exception();
		}
		if(pthread_cond_init(&m_cond,NULL)!=0){
			pthread_mutex_destroy(&m_mutex);
			throw std::exception();
		}
	}
	~Cond(){
		pthread_mutex_destroy(&m_mutex);
		pthread_cond_destroy(&m_cond);
	}

	bool wait(){
		int ret=0;
		pthread_mutex_lock(&m_mutex);
		ret=pthread_cond_wait(&m_cond,&m_mutex);//理解pthread_cond_wait后台实现的逻辑
		pthread_mutex_unlock(&m_mutex);
		return ret=0;
	}

	bool signal(){
		return pthread_cond_signal(&m_cond)==0;
	}
};

#endif








