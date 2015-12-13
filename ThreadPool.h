#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <assert.h>
#include <pthread.h>
#include "locker.h"

template<typename T>
class threadpool{
public:
	threadpool(int thread_numner=2, int max_request=10000);
	~threadpool();
	bool append(T* request);
private:
	static void* worker(void* arg);
	void run();
private:
	int m_thread_number;//线程池中的线程数量
	int m_max_request;
	bool m_stop;//终止标记
	pthread_t* m_threads;
	std::list<T*> m_workqueue;
	Sem m_sem;
	Locker m_locker;
};
template<typename T>
threadpool<T>::threadpool(int thread_numner,int max_request){
	m_thread_number=thread_numner;
	m_max_request=max_request;
	m_stop=false;
	assert(m_thread_number>0);
	assert(m_max_request>0);

	m_threads=new pthread_t[thread_numner];
	if(!m_threads){
		perror("m_threads error");
		return;
	}

	for(int i=0;i<thread_numner;i++){
		if(pthread_create(m_threads+i,NULL,worker,this)!=0){
			delete [] m_threads;
			throw std::exception();
		}
		printf("%d\n",i);
		if(pthread_detach(m_threads[i])){
			delete [] m_threads;
			throw std::exception();
		}
		printf("%d\n",i);
	}
}

template<typename T>
threadpool<T>::~threadpool(){
	delete [] m_threads;
	m_stop=true;
}

template<typename T>
void* threadpool<T>::worker(void * arg){
	threadpool* pool=(threadpool*) arg;
	printf("begin run \n");
	pool->run();
	//while(true){}
	return pool;
}

template<typename T>
void threadpool<T>::run(){
	printf("stop %d\n", m_stop);
	if(m_stop==false){
		printf("here1\n");
	}
	while(true){
		printf("here\n");

		m_sem.wait();
		m_locker.lock();
		if(m_workqueue.empty()){
			m_locker.unlock();
			continue;
		}
		T* request=m_workqueue.front();
		m_workqueue.pop_front();
		m_locker.unlock();
		if(!request){
			continue;
		}
		request->process();

	}
}

template<typename T>
bool threadpool<T>::append(T* request){
	m_workqueue.push_back(request);
	m_locker.unlock();
	m_sem.post();
	return true;
}

#endif
