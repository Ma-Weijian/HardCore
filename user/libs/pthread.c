#include <pthread.h>
#include <syscall.h>
#include <defs.h>
#include <string.h>
#include <stdio.h>
#include <ulib.h>

int pthread_create(pthread_t *newthread, void *(*fn)(void *), void *arg)
{
  return sys_clone(newthread, fn, arg, pthread_exit);
}

int pthread_join(pthread_t *newthread)
{
  waitpid(*newthread, NULL);
}

void pthread_exit(int error_code)
{
  exit(error_code);
}

void phtread_daemon(){
  waitpid(-1, NULL);
}