#ifndef __USER_LIBS_SYSCALL_H__
#define __USER_LIBS_SYSCALL_H__

// 暂时没法解决makefile make 时的冲突，只能把信号量的定义写两遍
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

int sys_exit(int error_code);
int sys_fork(void);
int sys_wait(int pid, int *store);
int sys_exec(const char *name, int argc, const char **argv);
int sys_yield(void);
int sys_kill(int pid);
int sys_getpid(void);
int sys_putc(int c);
int sys_pgdir(void);
int sys_sleep(unsigned int time);
size_t sys_gettime(void);

struct stat;
struct dirent;

int sys_open(const char *path, uint32_t open_flags);
int sys_close(int fd);
int sys_read(int fd, void *base, size_t len);
int sys_write(int fd, void *base, size_t len);
int sys_seek(int fd, off_t pos, int whence);
int sys_fstat(int fd, struct stat *stat);
int sys_fsync(int fd);
int sys_getcwd(char *buffer, size_t len);
int sys_mkdir(const char *path);
int sys_chdir(const char *path);
int sys_link(const char *old_path, const char *new_path);
int sys_rename(const char *old_path, const char *new_path);
int sys_unlink(const char *path);
int sys_getdirentry(int fd, struct dirent *dirent);
int sys_dup(int fd1, int fd2);
int sys_get_pdb(void *base); //get pdb from kernel
int sys_clone(int *thread_id, void *(*fn)(void *), void *arg, void (*exit)(int));
int sys_sem(semaphore_t *sem, int *value, int type);
// 节省系统调用，若
// type:
//    0  表示调用 sem_init     ，传入的 value 为 放置信号量的初值的地址
//    1  表示调用 sem_wait     ，传入的 value 为 NULL
//    2  表示调用 sem_post     ，传入的 value 为 NULL
//    3  表示调用 sem_getvalue ，传入的 value 为 放置信号量大小的的地址
int sys_nice(int pid, int prior);
int sys_shmem(uintptr_t * addr_store, size_t len, uint32_t mmap_flags);
int sys_brk(uintptr_t * brk_store);

#endif /* !__USER_LIBS_SYSCALL_H__ */

