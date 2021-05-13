#ifndef __USER_LIBS_SEMAPHORE_H__
#define __USER_LIBS_SEMAPHORE_H__

struct list_entry
{
    struct list_entry *prev, *next;
};

typedef struct list_entry list_entry_t;

typedef struct
{
    list_entry_t wait_head;
} wait_queue_t;

typedef struct
{
    int value;
    wait_queue_t wait_queue;
} semaphore_t;


int sem_init(semaphore_t *sem, int value);
int sem_wait(semaphore_t *sem);
int sem_post(semaphore_t *sem);
int sem_getvalue(semaphore_t *sem, int *value);

#endif /* !__USER_LIBS_SEMAPHORE_H__ */