#include <defs.h>
#include <unistd.h>
#include <stdarg.h>
#include <syscall.h>
#include <stat.h>
#include <dirent.h>


#define MAX_ARGS            5

static inline int
syscall(int num, ...) {
    va_list ap;
    va_start(ap, num);
    uint32_t a[MAX_ARGS];
    int i, ret;
    for (i = 0; i < MAX_ARGS; i ++) {
        a[i] = va_arg(ap, uint32_t);
    }
    va_end(ap);

    asm volatile (
        "int %1;"
        : "=a" (ret)
        : "i" (T_SYSCALL),
          "a" (num),
          "d" (a[0]),
          "c" (a[1]),
          "b" (a[2]),
          "D" (a[3]),
          "S" (a[4])
        : "cc", "memory");
    return ret;
}

int
sys_exit(int error_code) {
    return syscall(SYS_exit, error_code);
}

int
sys_fork(void) {
    return syscall(SYS_fork);
}

int
sys_wait(int pid, int *store) {
    return syscall(SYS_wait, pid, store);
}

int
sys_yield(void) {
    return syscall(SYS_yield);
}

int
sys_kill(int pid) {
    return syscall(SYS_kill, pid);
}

int
sys_getpid(void) {
    return syscall(SYS_getpid);
}

int
sys_putc(int c) {
    return syscall(SYS_putc, c);
}

int
sys_pgdir(void) {
    return syscall(SYS_pgdir);
}

int
sys_sleep(unsigned int time) {
    return syscall(SYS_sleep, time);
}

size_t
sys_gettime(void) {
    return syscall(SYS_gettime);
}

int
sys_exec(const char *name, int argc, const char **argv) {
    return syscall(SYS_exec, name, argc, argv);
}

int
sys_open(const char *path, uint32_t open_flags) {
    return syscall(SYS_open, path, open_flags);
}

int
sys_close(int fd) {
    return syscall(SYS_close, fd);
}

int
sys_read(int fd, void *base, size_t len) {
    return syscall(SYS_read, fd, base, len);
}

int
sys_write(int fd, void *base, size_t len) {
    return syscall(SYS_write, fd, base, len);
}

int
sys_seek(int fd, off_t pos, int whence) {
    return syscall(SYS_seek, fd, pos, whence);
}

int
sys_fstat(int fd, struct stat *stat) {
    return syscall(SYS_fstat, fd, stat);
}

int
sys_fsync(int fd) {
    return syscall(SYS_fsync, fd);
}

int
sys_getcwd(char *buffer, size_t len) {
    return syscall(SYS_getcwd, buffer, len);
}

int sys_mkdir(const char *path) 
{
    return syscall(SYS_mkdir, path);
}

int sys_chdir(const char *path)
{
    return syscall(SYS_chdir, path);
}

int sys_link(const char *old_path, const char *new_path) 
{
    return syscall(SYS_link, old_path, new_path);
}

int sys_rename(const char *old_path, const char *new_path)
{
    return syscall(SYS_rename, old_path, new_path);
}

int sys_unlink(const char *path)
{
    return syscall(SYS_unlink, path);
}

int
sys_getdirentry(int fd, struct dirent *dirent) {
    return syscall(SYS_getdirentry, fd, dirent);
}

int
sys_dup(int fd1, int fd2) {
    return syscall(SYS_dup, fd1, fd2);
}

int sys_get_pdb(void *base)
{
    return syscall(SYS_get_pdb, base);
}

int sys_clone(int *thread_id, void *(*fn)(void *), void *arg, void (*exit)(int))
{
    return syscall(SYS_clone, thread_id, fn, arg, exit);
}

int sys_sem(semaphore_t *sem, int *value, int type)
{
    return syscall(SYS_sem, sem, value, type);
}

int sys_nice(int pid, int prior)
{
    return syscall(SYS_nice, pid , prior);
}

int 
sys_shmem(uintptr_t * addr_store, size_t len, uint32_t mmap_flags)
{
	return syscall(SYS_shmem, addr_store, len, mmap_flags);
}

int 
sys_brk(uintptr_t * brk_store)
{
	return syscall(SYS_brk, brk_store);
}

void sys_check_alloc_page()
{
	syscall(SYS_check_alloc_page);
}

void sys_check_swap()
{
	syscall(SYS_check_swap);
}

int sys_fifo_check_swap()
{
	return syscall(SYS_fifo_check_swap);
}