#ifndef _COMMON_H
#define _COMMON_H 1
struct Message{
	int rid; 										// request id
	pid_t pid; 										// process id
	pthread_t tid;									// thread id
	int tskload;									// task load
	int tskres;										// task result
};
#endif // _COMMON_H
