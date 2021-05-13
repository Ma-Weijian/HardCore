#include <defs.h>
#include <string.h>
#include <stdio.h>
#include <ulib.h>
#include <semaphore.h>

int sem_init(semaphore_t *sem, int value)
{
  return sys_sem(sem, &value, 0);
}
int sem_post(semaphore_t *sem)
{
  return sys_sem(sem, NULL, 1);
}
int sem_wait(semaphore_t *sem)
{
  return sys_sem(sem, NULL, 2);
}
int sem_getvalue(semaphore_t *sem, int *value)
{
  return sys_sem(sem, value, 3);
}