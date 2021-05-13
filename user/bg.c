#include <pthread.h>
#include <stdio.h>

pthread_t thread_id[12];

void loop(void *x)
{
  while (1)
    ;
}

int main()
{
  for (int i = 0; i < 12; i++)
    pthread_create(&thread_id[i], loop, NULL);
  for (int i = 0; i < 12; i++)
    pthread_join(&thread_id[i]);
  return 0;
}