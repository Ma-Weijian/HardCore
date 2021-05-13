#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <defs.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>
#include <skew_heap.h>


// process's state in his life cycle
enum proc_state {
    PROC_UNINIT = 0,  // uninitialized
    PROC_SLEEPING,    // sleeping
    PROC_RUNNABLE,    // runnable(maybe running)
    PROC_ZOMBIE,      // almost dead, and wait parent proc to reclaim his resource
};

// Saved registers for kernel context switches.
// Don't need to save all the %fs etc. segment registers,
// because they are constant across kernel contexts.
// Save all the regular registers so we don't need to care
// which are caller save, but not the return register %eax.
// (Not saving %eax just simplifies the switching code.)
// The layout of context must match code in switch.S.
struct context {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
};

#define PROC_NAME_LEN               50
#define MAX_PROCESS                 4096
#define MAX_PID                     (MAX_PROCESS * 2)
#define MAX_THREAD 16

extern list_entry_t proc_list;

struct inode;

struct proc_struct {
    enum proc_state state;                      // Process state
    int pid;                                    // Process ID
    int runs;                                   // the running times of Proces
    uintptr_t kstack;                           // Process kernel stack
    volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
    struct proc_struct *parent;                 // the parent process
    struct mm_struct *mm;                       // Process's memory management field
    struct context context;                     // Switch here to run process
    struct trapframe *tf;                       // Trap frame for current interrupt
    uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
    uint32_t flags;                             // Process flag
    char name[PROC_NAME_LEN + 1];               // Process name
    list_entry_t list_link;                     // Process link list
    list_entry_t hash_link;                     // Process hash list
    int exit_code;                              // exit code (be sent to parent proc)
    uint32_t wait_state;                        // waiting state
    struct proc_struct *cptr, *yptr, *optr;     // relations between processes
    struct run_queue *rq;                       // running queue contains Process
    list_entry_t run_link;                      // the entry linked in run queue
    int time_slice;                             // time slice for occupying the CPU
    struct files_struct *filesp;                // the file related info(pwd, files_count, files_array, fs_semaphore) of process
    uint32_t vruntime;                          // cfs scheduler : virtual run time
    uint32_t cfs_prior;                         // cfs scheduler : the prior of this process (less have more prior), the mininum vruntime procee will be schedule
    uint32_t stride;                            // stride scheduler : the proccess with mininum strider will be schedule
    uint32_t stride_prior;                      // stride scheduler : the prior of this process (less have more prior)
    int is_thread;                              // 标志该进程是否是一个子线程
    int stack_num;                              // 标志该子线程占用了父进程的哪一个栈帧，is_thread = 1 才有效
    int stack[MAX_THREAD];                      // 每个主进程能够开启16个线程（包括主线程（自己）在内），每个块为 0 表示该块的栈没有被占用，不为 0 表示被占用，且值是该子线程的pid
};

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

#define PF_EXITING                  0x00000001      // getting shutdown

#define WT_INTERRUPTED               0x80000000                    // the wait state could be interrupted
#define WT_CHILD                    (0x00000001 | WT_INTERRUPTED)  // wait child process
#define WT_KSEM                      0x00000100                    // wait kernel semaphore
#define WT_TIMER                    (0x00000002 | WT_INTERRUPTED)  // wait timer
#define WT_KBD                      (0x00000004 | WT_INTERRUPTED)  // wait the input of keyboard

#define le2proc(le, member)         \
    to_struct((le), struct proc_struct, member)

extern struct proc_struct *idleproc, *initproc, *current;

void proc_init(void);
void proc_run(struct proc_struct *proc);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name);
char *get_proc_name(struct proc_struct *proc);
void cpu_idle(void) __attribute__((noreturn));

struct proc_struct *find_proc(int pid);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_exit(int error_code);
int do_yield(void);
int do_execve(const char *name, int argc, const char **argv);
int do_wait(int pid, int *code_store);
int do_kill(int pid);
int do_clone(void *(*fn)(void *), void *arg, void (*exit)(int));
int do_sleep(unsigned int time);
int get_pdb(void *base);
void pdb2pdb_user(struct proc_struct *proc, struct proc_struct_user *pdb_user);
int current_have_kid();
void kill_all_zombie_ch_process();
int is_ancestral_thread(struct proc_struct *proc);
int do_kill_all_thread(int pid);
#endif /* !__KERN_PROCESS_PROC_H__ */
