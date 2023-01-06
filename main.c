#include"pthreadpoll.h"

void taskFunc(void* arg)
{
    int num = *(int*)arg;
    printf("thread %ld is working, number = %d\n",
        pthread_self(), num);
    sleep(1);
}

int main()
{
    // 创建线程池
    ThreadPoll* poll = threadPollCreate(3, 10, 200);
    for (int i = 0; i < 200; ++i)
    {
        int* num = (int*)malloc(sizeof(int));
        *num = i;
        threadPollAdd(poll, taskFunc, num);
    }

    sleep(15);

    threadPollDestroy(poll);
    return 0;
}
