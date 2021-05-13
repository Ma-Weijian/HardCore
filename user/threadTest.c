#include <pthread.h>
#include <stdio.h>

pthread_t test_thread, test_thread_plus;

int sum = 10;

void add(int *x)
{
  sum += *x;
}

void add_plus(int *x)
{
  sum += *x;
}

int main()
{
  int y = 10;
  pthread_create(&test_thread, add, &y);
  pthread_create(&test_thread_plus, add_plus, &y);
  // pthread_join(&test_thread);
  waitpid(-1, NULL);
  cprintf("sum is %d now\n", sum);
  // pthread_exit(0);
  return 0;
}
