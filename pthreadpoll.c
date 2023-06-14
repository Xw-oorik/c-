#include"pthreadpoll.h"

const int NUMBER=2;//一次添加两个
//任务结构体
typedef struct Task
{
    void (*function)(void* arg);
    void *arg;
}Task;
//线程池结构体
struct ThreadPoll
{
    //任务队列
    Task* taskQ;
    int queueCapcity;//任务队列容量
    int queueSize;//当前任务个数
    int queueFront;//队头 取数据
    int queueRear;//队尾 放数据

    pthread_t managerID;//管理者线程id
    pthread_t * threadIDs;//工作线程id 若干个
    int minNum;//最小线程数
    int maxNum;//最大
    int busyNum;//当前正在忙着工作的线程个数
    int liveNum;//当前存活的工作线程，活着不一定在工作
    int exitNum;//要杀死的 线程个数，不干活多余的
    pthread_mutex_t mutexpoll;//锁整个线程池
    pthread_mutex_t mutexBusy;//锁busyNum

    pthread_cond_t notFull;//任务队列是不是满
    pthread_cond_t notEmpty;//任务队列是不是空  

    int shutdown;//要不要销毁，1销毁，0不销毁
};

//给线程池添加任务
void threadPollAdd(ThreadPoll* poll,void(*func)(void*),void*arg)
{
    pthread_mutex_lock(&poll->mutexpoll);

    while(poll->queueSize==poll->queueCapcity&&!poll->shutdown)
    {
        //阻塞生产者线程
        pthread_cond_wait(&poll->notFull,&poll->mutexpoll);
    }
    if(poll->shutdown)
    {
        pthread_mutex_unlock(&poll->mutexpoll);
        return ;
    }
    //添加任务
    poll->taskQ[poll->queueRear].function=func;
    poll->taskQ[poll->queueRear].arg=arg;
    poll->queueRear=(poll->queueRear+1)%poll->queueCapcity;
    poll->queueSize++;

    //唤醒消费者
    pthread_cond_signal(&poll->notEmpty);
    pthread_mutex_unlock(&poll->mutexpoll);
}
//工作线程 读任务队列
void *worker(void* arg)
{
    ThreadPoll* poll=(ThreadPoll*)arg;

    while(1)
    {
        pthread_mutex_lock(&poll->mutexpoll);
        //当前任务队列是否为空
        while(poll->queueSize==0&&!poll->shutdown)
        {
            //阻塞工作线程
            pthread_cond_wait(&poll->notEmpty,&poll->mutexpoll);//拿锁进来，条件不满足，释放锁再次等着条件成立，阻塞在这
            //判断是不是要销毁线程
            if(poll->exitNum>0)
            {
                //走到这说明管理者把他唤醒了，他拿到了互斥锁，锁上了
                //在他退出自杀前要把锁解开，免得后面死锁
                poll->exitNum--;
                if(poll->liveNum>poll->minNum)
                {
                    poll->liveNum--;
                    pthread_mutex_unlock(&poll->mutexpoll);
                    threadExit(poll);
                }
            }
        }
        //开始消费
        //判断线程池是否被关闭
        if(poll->shutdown)
        {
            pthread_mutex_unlock(&poll->mutexpoll);
            threadExit(poll);
        }
        //从任务队列中取出一个任务
        Task task;
        task.function=poll->taskQ[poll->queueFront].function;
        task.arg=poll->taskQ[poll->queueFront].arg;
        //移动头结点，任务队列弄成环形队列那种
        poll->queueFront=(poll->queueFront+1)%poll->queueCapcity;
        poll->queueSize--;
        //解锁唤醒生产者
        pthread_cond_signal(&poll->notFull);
        pthread_mutex_unlock(&poll->mutexpoll);

        //忙工作线程数+1
        printf("thread %ld start working...\n",pthread_self());
        pthread_mutex_lock(&poll->mutexBusy);
        poll->busyNum++;
        pthread_mutex_unlock(&poll->mutexBusy);

        task.function(task.arg);//调用执行函数
        free(task.arg);
        task.arg=NULL;
        printf("thread %ld end working...\n",pthread_self());
        //执行结束 忙工作线程数-1
        pthread_mutex_lock(&poll->mutexBusy);
        poll->busyNum--;
        pthread_mutex_unlock(&poll->mutexBusy);
    }
    return NULL;

}
//管理者线程
void *manager(void* arg)
{
    ThreadPoll* poll=(ThreadPoll*)arg;
    while(!poll->shutdown)
    {
        //每隔3s检测一次
        sleep(3);
        //取出线程池中任务的数量和当前线程的数量
        pthread_mutex_lock(&poll->mutexpoll);
        int queueSize=poll->queueSize;
        int liveNum=poll->liveNum;
        pthread_mutex_unlock(&poll->mutexpoll);

        //取出忙的线程数量
        pthread_mutex_lock(&poll->mutexBusy);
        int busyNum=poll->busyNum;
        pthread_mutex_unlock(&poll->mutexBusy);

        //添加线程
        //任务个数大于存活的线程个数，并且存活线程数小于最大线程数
        if(queueSize>liveNum&&liveNum<poll->maxNum)
        {
            pthread_mutex_lock(&poll->mutexpoll);
            int count=0;
            for(int i=0;i<poll->maxNum&&count<NUMBER&&poll->liveNum<poll->maxNum;++i)
            {
                if(poll->threadIDs[i]==0)//可以使用,位置空闲
                {
                    pthread_create(&poll->threadIDs[i],NULL,worker,poll);
                    count++;
                    poll->liveNum++;
                } 
            }
            pthread_mutex_unlock(&poll->mutexpoll);
        }
        //销毁线程
        //忙的线程*2小于存活线程并且存活线程大于最小线程数
        if(busyNum*2<liveNum&&liveNum>poll->minNum)
        {
            pthread_mutex_lock(&poll->mutexpoll);
            poll->exitNum=NUMBER;
            pthread_mutex_unlock(&poll->mutexpoll);
            //让工作的线程自杀
            //把阻塞着的不工作的线程唤醒，唤醒的抢到了互斥锁的线程就往下走，自己退出
            for(int i=0;i<NUMBER;++i)
            {
                pthread_cond_signal(&poll->notEmpty);
            }
        }
    }
    return NULL;
}
void threadExit(ThreadPoll* poll)
{
    pthread_t tid=pthread_self();
    for(int i=0;i<poll->maxNum;++i)
    {
        if(poll->threadIDs[i]==tid)
        {
            poll->threadIDs[i]=0;
            printf("threadExit() called,%ld exiting...\n",tid);
            break;
        }
    }
    pthread_exit(NULL);
}
//创建线程池并初始化
ThreadPoll * threadPollCreate(int mins,int maxs,int queueSize)
{
    ThreadPoll *poll =(ThreadPoll*)malloc(sizeof(ThreadPoll));
    
    do
    {

    if(poll==NULL)
    {
        printf("create threadpoll fail...\n");
        break;
    }
    //工作线程id
    poll->threadIDs=(pthread_t*)malloc(sizeof(pthread_t)*maxs);
    if(poll->threadIDs==NULL)
    {
        printf("create threadIDs fail...\n");
        break;
    }
    memset(poll->threadIDs,0,sizeof(pthread_t)*maxs);//初始化成0,0代表可以用
    //初始化
    poll->minNum=mins;
    poll->maxNum=maxs;
    poll->busyNum=0;
    poll->liveNum=mins;
    poll->exitNum=0;

    if(pthread_mutex_init(&poll->mutexpoll,NULL)!=0||
       pthread_mutex_init(&poll->mutexBusy,NULL)!=0||
       pthread_cond_init(&poll->notFull,NULL)!=0||
       pthread_cond_init(&poll->notEmpty,NULL)!=0 
    )
    {
        printf("mutex or cond fail...\n");
        break;
    }
    //任务队列初始化
    poll->taskQ=(Task*)malloc(sizeof(Task)*queueSize);
    poll->queueCapcity=queueSize;
    poll->queueSize=0;
    poll->queueFront=0;
    poll->queueRear=0;
    poll->shutdown=0;
    //创建线程
    pthread_create(&poll->managerID,NULL,manager,poll);
    for(int i=0;i<mins;++i)
    {
        pthread_create(&poll->threadIDs[i],NULL,worker,poll);
    }
    return poll;

    }while(0);
   
    //释放资源
    if(poll&&poll->threadIDs)
    {
        free(poll->threadIDs);
    }
    if(poll&&poll->taskQ)
    {
        free(poll->taskQ);
    }
    if(poll)
    {
        free(poll);
    }
    return NULL;

}
//获取线程池中工作线程个数
int threadPollBusynum(ThreadPoll* poll)
{
    pthread_mutex_lock(&poll->mutexBusy);
    int busyNum=poll->busyNum;
    pthread_mutex_unlock(&poll->mutexBusy);
    return busyNum;
}
//获取活着的线程个数
int threadPollLivenum(ThreadPoll* poll)
{
    pthread_mutex_lock(&poll->mutexpoll);
    int liveNum=poll->liveNum;
    pthread_mutex_unlock(&poll->mutexpoll);
    return liveNum;
}
//销毁线程池
int threadPollDestroy(ThreadPoll* poll)
{
    if(poll==NULL)return -1;
    //关闭线程池
    poll->shutdown=1;
    //阻塞回收管理者线程
    pthread_join(poll->managerID,NULL);
    //唤醒阻塞消费者线程
    for(int i=0;i<poll->liveNum;++i)
    {
        pthread_cond_signal(&poll->notEmpty);
    }
    //释放堆内存
    if(poll->taskQ)
    {
        free(poll->taskQ);
    }
    if(poll->threadIDs)
    {
        free(poll->threadIDs);
    }
  
    pthread_mutex_destroy(&poll->mutexpoll);
    pthread_mutex_destroy(&poll->mutexBusy);
    pthread_cond_destroy(&poll->notEmpty);
    pthread_cond_destroy(&poll->notFull);  
    free(poll);
    poll=NULL;
    return 0;
}
    
