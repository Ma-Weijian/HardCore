// 利用pthread库，通过其中的互斥锁和信号量来实现读者优先的读者-写者问题。
#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>

static unsigned long mynest = 23333;

int myrand(void)
{
  mynest = mynest * 1103515245 + 12345;
  return ((unsigned)(mynest / 65536) % 32768);
}

#define READER_NUM 10 //number of reader
#define WRITER_NUM 3  //number of writer

pthread_t reader_thread[READER_NUM]; //thread id of reader
pthread_t writer_thread[WRITER_NUM]; //thread id of writer

int readercount = 0;

int buffer = 100;

semaphore_t wrt; //use by the first enter reader and the last leave reader

semaphore_t mutex; //protect readercount update correctly

int try_time = 100;

void *reader(void *arg)
{
  int time = try_time;
  while (time--)
  {
    sem_wait(&mutex); //protect update of readercount
    readercount++;
    //if writer is writing, a reader is waiting on wrt (because the execute of pthread_mutex_lock(&wrt))
    //other reader is waiting on mutex, because the thread block on wrt don't realse the mutex
    if (readercount == 1)
      sem_wait(&wrt); //lock when the first reader enter,
    sem_post(&mutex);

    cprintf("reader read the value of buffer is ：%d\n", buffer); //read

    sem_wait(&mutex);
    readercount--;
    if (readercount == 0)
      sem_post(&wrt); //unlock when the last reader leave, so wirter could write
    sem_post(&mutex);
    sleep(1);
  }
}
void *writer(void *arg)
{
  int time = try_time;
  while (time--)
  {
    sem_wait(&wrt); //enter when no reader

    buffer = myrand() % 100; //write
    cprintf("writer change the orignal value to ：%d\n", buffer);

    sem_post(&wrt);
    sleep(1);
  }
}

int main()
{
  sem_init(&wrt, 1);
  sem_init(&mutex, 1);
  for (int i = 0; i < READER_NUM; i++)
    pthread_create(&reader_thread[i], reader, NULL);
  for (int i = 0; i < WRITER_NUM; i++)
    pthread_create(&writer_thread[i], writer, NULL);
  for (int i = 0; i < READER_NUM; i++)
    pthread_join(&reader_thread[i]);
  for (int i = 0; i < WRITER_NUM; i++)
    pthread_join(&writer_thread[i]);
  return 0;
}