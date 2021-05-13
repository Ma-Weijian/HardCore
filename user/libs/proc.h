#ifndef __USER_LIBS_PROC_H__
#define __USER_LIBS_PROC_H__

#include <defs.h>

#define WT_INTERRUPTED               0x80000000                    // the wait state could be interrupted
#define WT_CHILD                    (0x00000001 | WT_INTERRUPTED)  // wait child process
#define WT_KSEM                      0x00000100                    // wait kernel semaphore
#define WT_TIMER                    (0x00000002 | WT_INTERRUPTED)  // wait timer
#define WT_KBD                      (0x00000004 | WT_INTERRUPTED)  // wait the input of keyboard

#define PROC_NAME_LEN 50

// process's state in his life cycle
enum proc_state
{
    PROC_UNINIT = 0, // uninitialized
    PROC_SLEEPING,   // sleeping
    PROC_RUNNABLE,   // runnable(maybe running)
    PROC_ZOMBIE,     // almost dead, and wait parent proc to reclaim his resource
};

typedef int bool;
typedef unsigned int uint32_t;

// used by system call
struct proc_struct_user
{
    enum proc_state state;        // Process state
    int pid;                      // Process ID
    int runs;                     // the running times of Proces
    int parent;                   // the parent process
    char name[PROC_NAME_LEN + 1]; // Process name
    uint32_t wait_state;          // waiting state
    uint32_t prior;               // the prior of this process (less have more prior), the mininum vruntime procee will be schedule
    int is_thread;                // 标志该进程是否是一个子线程
    int total_page;               // 总页数
    int free_page;                // 未被使用的页面数量
};

int get_pdb(void *base);
int nice(int pid, int prior);

#endif /* !__USER_LIBS_PROC_H__ */