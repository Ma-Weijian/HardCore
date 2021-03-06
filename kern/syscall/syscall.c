#include <defs.h>
#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <assert.h>
#include <clock.h>
#include <stat.h>
#include <dirent.h>
#include <sysfile.h>
#include <sem.h>
#include <kmalloc.h>
#include <swap.h>
#include <swap_fifo.h>
static int
sys_exit(uint32_t arg[]) {
    int error_code = (int)arg[0];
    if (is_ancestral_thread(current)) //祖宗进程只有在所有子线程全部释放后才能退出
        while (current_have_kid())    //只要有儿子就等着
            do_wait(0, NULL);
    return do_exit(error_code);
}

static int
sys_fork(uint32_t arg[]) {
    struct trapframe *tf = current->tf;
    uintptr_t stack = tf->tf_esp;
    return do_fork(0, stack, tf);
}

static int
sys_wait(uint32_t arg[]) {
    int pid = (int)arg[0];
    int *store = (int *)arg[1];
    if (pid == -1)
    {
        while (current_have_kid())
            do_wait(0, store);
    }
    return do_wait(pid, store);
}

static int
sys_exec(uint32_t arg[]) {
    const char *name = (const char *)arg[0];
    int argc = (int)arg[1];
    const char **argv = (const char **)arg[2];
    return do_execve(name, argc, argv);
}

static int
sys_yield(uint32_t arg[]) {
    return do_yield();
}

static int
sys_kill(uint32_t arg[]) {
    int pid = (int)arg[0];
    return do_kill_all_thread(pid);
}

static int
sys_getpid(uint32_t arg[]) {
    return current->pid;
}

static int
sys_putc(uint32_t arg[]) {
    int c = (int)arg[0];
    cputchar(c);
    return 0;
}

static int
sys_pgdir(uint32_t arg[]) {
    print_pgdir();
    return 0;
}

static uint32_t
sys_gettime(uint32_t arg[]) {
    return (int)ticks;
}

static int
sys_sleep(uint32_t arg[]) {
    unsigned int time = (unsigned int)arg[0];
    return do_sleep(time);
}

static int
sys_open(uint32_t arg[]) {
    const char *path = (const char *)arg[0];
    uint32_t open_flags = (uint32_t)arg[1];
    return sysfile_open(path, open_flags);
}

static int
sys_close(uint32_t arg[]) {
    int fd = (int)arg[0];
    return sysfile_close(fd);
}

static int
sys_read(uint32_t arg[]) {
    int fd = (int)arg[0];
    void *base = (void *)arg[1];
    size_t len = (size_t)arg[2];
    return sysfile_read(fd, base, len);
}

static int
sys_write(uint32_t arg[]) {
    int fd = (int)arg[0];
    void *base = (void *)arg[1];
    size_t len = (size_t)arg[2];
    return sysfile_write(fd, base, len);
}

static int
sys_seek(uint32_t arg[]) {
    int fd = (int)arg[0];
    off_t pos = (off_t)arg[1];
    int whence = (int)arg[2];
    return sysfile_seek(fd, pos, whence);
}

static int
sys_fstat(uint32_t arg[]) {
    int fd = (int)arg[0];
    struct stat *stat = (struct stat *)arg[1];
    return sysfile_fstat(fd, stat);
}

static int
sys_fsync(uint32_t arg[]) {
    int fd = (int)arg[0];
    return sysfile_fsync(fd);
}

static int
sys_getcwd(uint32_t arg[]) {
    char *buf = (char *)arg[0];
    size_t len = (size_t)arg[1];
    return sysfile_getcwd(buf, len);
}

static int sys_mkdir(uint32_t arg[]) {
    const char *path = (const char *)arg[0];
    return sysfile_mkdir(path);
}

static int sys_chdir(uint32_t arg[]) {
    const char *path = (const char *)arg[0];
    return sysfile_chdir(path);
}

static int sys_link(uint32_t arg[]) 
{
    const char *old_path = (const char *)arg[0];
    const char *new_path = (const char *)arg[1];
    return sysfile_link(old_path, new_path);
}

static int sys_rename(uint32_t arg[]) 
{
    const char *old_path = (const char *)arg[0];
    const char *new_path = (const char *)arg[1];
    return sysfile_rename(old_path, new_path);
}

static int sys_unlink(uint32_t arg[]) 
{
    const char *path = (const char *)arg[0];
    return sysfile_unlink(path);
}

static int
sys_getdirentry(uint32_t arg[]) {
    int fd = (int)arg[0];
    struct dirent *direntp = (struct dirent *)arg[1];
    return sysfile_getdirentry(fd, direntp);
}

static int
sys_dup(uint32_t arg[]) {
    int fd1 = (int)arg[0];
    int fd2 = (int)arg[1];
    return sysfile_dup(fd1, fd2);
}

static int
sys_get_pdb(uint32_t arg[]) {
    void * base = (void *)arg[0];
    return get_pdb(base);
}

static int
sys_clone(uint32_t arg[])
{
    int *thread_id = (void *)arg[0];
    void *(*fn)(void *) = (void *)arg[1];
    void *argv = (void *)arg[2];
    void (*exit)(int) = (void *)arg[3];
    *thread_id = do_clone(fn, argv, exit);
    if (*thread_id > 0)
        return 0;
    else
        return -1;
}

static int
sys_sem(uint32_t arg[])
{
    semaphore_t *sem = (semaphore_t *)arg[0];
    int *value = (int *)arg[1];
    int type = (int)arg[2];
    switch (type)
    {
    case 0:
        sem_init(sem, *value);
        break;
    case 1:
        up(sem);
        break;
    case 2:
        down(sem);
        break;
    case 3:
        *value = sem->value;
        break;
    default:
        return -1;
    }
    return 0;
}

static int
sys_nice(uint32_t arg[])
{
    int pid = (int)arg[0];
    int prior = (int)arg[1];
    struct proc_struct * proc = find_proc(pid);
    if (proc == NULL){
        return -1;
    }
    proc->cfs_prior = prior;
    proc->stride_prior = prior;
    return 0;
}

static int
sys_brk(uint32_t arg[]) {
	uintptr_t *brk_store = (uintptr_t *) arg[0];
    //cprintf("going to do_brk\n");
	return do_brk(brk_store);
}

static int
sys_shmem(uint32_t arg[]) {

}

static void
sys_check_alloc_page()
{
	kmalloc_init();
}

static void
sys_check_swap()
{
	check_swap();
}

static int 
sys_fifo_check_swap()
{
	return	_fifo_check_swap();
}
static int (*syscalls[])(uint32_t arg[]) = {
    [SYS_exit] sys_exit,
    [SYS_fork] sys_fork,
    [SYS_clone] sys_clone,
    [SYS_wait] sys_wait,
    [SYS_exec] sys_exec,
    [SYS_yield] sys_yield,
    [SYS_kill] sys_kill,
    [SYS_getpid] sys_getpid,
    [SYS_putc] sys_putc,
    [SYS_pgdir] sys_pgdir,
    [SYS_gettime] sys_gettime,
    [SYS_sleep] sys_sleep,
    [SYS_open] sys_open,
    [SYS_close] sys_close,
    [SYS_read] sys_read,
    [SYS_write] sys_write,
    [SYS_seek] sys_seek,
    [SYS_fstat] sys_fstat,
    [SYS_fsync] sys_fsync,
    [SYS_getcwd] sys_getcwd,
    [SYS_chdir] sys_chdir,
    [SYS_mkdir] sys_mkdir,
    [SYS_link] sys_link,
    [SYS_unlink] sys_unlink,
    [SYS_getdirentry] sys_getdirentry,
    [SYS_dup] sys_dup,
    [SYS_get_pdb] sys_get_pdb,
    [SYS_sem] sys_sem,
    [SYS_nice] sys_nice,
    [SYS_brk] sys_brk,
    [SYS_shmem] sys_shmem,
	[SYS_check_alloc_page] sys_check_alloc_page,
	[SYS_check_swap] sys_check_swap,
	[SYS_fifo_check_swap] sys_fifo_check_swap
};

#define NUM_SYSCALLS        ((sizeof(syscalls)) / (sizeof(syscalls[0])))

void
syscall(void) {
    struct trapframe *tf = current->tf;
    uint32_t arg[5];
    int num = tf->tf_regs.reg_eax;
    if (num >= 0 && num < NUM_SYSCALLS) {
        if (syscalls[num] != NULL) {
            arg[0] = tf->tf_regs.reg_edx;
            arg[1] = tf->tf_regs.reg_ecx;
            arg[2] = tf->tf_regs.reg_ebx;
            arg[3] = tf->tf_regs.reg_edi;
            arg[4] = tf->tf_regs.reg_esi;
            tf->tf_regs.reg_eax = syscalls[num](arg);
            return ;
        }
    }
    print_trapframe(tf);
    panic("undefined syscall %d, pid = %d, name = %s.\n",
            num, current->pid, current->name);
}

