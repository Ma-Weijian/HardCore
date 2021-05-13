#include <pthread.h>
#include <stdio.h>

pthread_t test_thread;
pthread_t deep;

int sum = 10;

void deepadd()
{
  deepadd();
}

void add(int *x)
{
  cprintf("before add sum is %d now\n", sum);
  int y = 20;
  sum += y;
  pthread_create(&deep, deepadd, NULL);
  pthread_join(&deep);
}

int main()
{
  int y = 10;
  pthread_create(&test_thread, add, &y);
  pthread_join(&test_thread);
  cprintf("after main sum is %d now\n", sum);
  return 0;
}