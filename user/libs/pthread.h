#ifndef __USER_LIBS_THREAD_H__
#define __USER_LIBS_THREAD_H__

#include <defs.h>

typedef int pthread_t;

void pthread_exit(int error_code);

// Create a new thread, starting with execution of START-ROUTINE
// getting passed ARG. Creation attributed come from ATTR. The new
// handle is stored in *NEWTHREAD.

// On  success,  pthread_create() returns 0; on error, 
// it returns an error number, and the contents
//  of *thread are undefined.
int pthread_create(pthread_t *newthread, void *(*fn)(void *), void *arg);

int pthread_join(pthread_t *newthread);

void phtread_daemon();

#endif /* !__USER_LIBS_THREAD_H__ */