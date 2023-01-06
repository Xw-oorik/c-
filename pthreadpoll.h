#ifndef _PTHREADPOLL_H
#define _PTHREADPOLL_H
//任务队列
//工作线程
//管理者线程
#include<pthread.h>
#include <stdio.h> 
#include<stdlib.h>
#include <unistd.h> 
#include<string.h>

typedef struct ThreadPoll ThreadPoll;
//创建线程池并初始化
ThreadPoll * threadPollCreate(int mins,int maxs,int queueSize);
void *worker(void* arg);
void *manager(void* arg);
void threadExit(ThreadPoll* poll);
//销毁线程池
int threadPollDestroy(ThreadPoll* poll);
//给线程池添加任务
void threadPollAdd(ThreadPoll* poll,void(*func)(void*),void*arg);
//获取线程池中工作线程个数
int threadPollBusynum(ThreadPoll* poll);
//获取活着的线程个数
int threadPollLivenum(ThreadPoll* poll);
#endif