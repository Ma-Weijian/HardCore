// #include <semaphore.h>

// semaphore_t i;

// int main()
// {
//   sem_init(&i, 6);
//   return 0;
// }
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

semaphore_t empty, full;

pthread_t produ, consu;

int commodity = 1;

void *producer(void *nonuse)
{
  cprintf("producer start!\n");
  int time = 10;
  do
  {
    sem_wait(&empty);
    sleep(10);
    commodity += 1;
    cprintf("producer produce product num is %d\n", commodity);
    // if (commodity != 0 || commodity != 1)
    // {
    //   cprintf("sem failed! exit\n");
    //   pthread_exit(0);
    // }
    sem_post(&full);

  } while (time--);
}

void *consumer(void *nonuse)
{
  cprintf("consumer start!\n");
  int time = 10;
  do
  {
    sem_wait(&full);
    sleep(10);
    commodity -= 1;
    cprintf("consumer consume product num is %d\n", commodity);
    // if (commodity != 0 || commodity != 1)
    // {
    //   cprintf("sem failed! exit\n");
    //   pthread_exit(0);
    // }
    sem_post(&empty);
  } while (time--);
}

int main(int argc, char const *argv[])
{
  cprintf("producer&consumer start!\n");
  sem_init(&empty, 0);
  sem_init(&full, 1);
  pthread_create(&produ, producer, NULL);
  pthread_create(&consu, consumer, NULL);
  pthread_join(&produ);
  pthread_join(&consu);
  cprintf("producer&consumer end!\n");
  return 0;
}
