#ifndef __KERN_FS_FS_H__
#define __KERN_FS_FS_H__

#include <defs.h>
#include <mmu.h>
#include <sem.h>
#include <atomic.h>

#define SECTSIZE            512
#define PAGE_NSECT          (PGSIZE / SECTSIZE)

#define SWAP_DEV_NO         1
#define DISK0_DEV_NO        2
#define DISK1_DEV_NO        3

void fs_init(void);
void fs_cleanup(void);

struct inode;
struct file;

/**
 * 进程相关文件结构
 * pwd       进程当前执行目录的内存inode指针
 * fd_array  进程打开文件的数组
 * files     访问此文件的线程数量
 * files_sem 确保对进程控制块fs_struct的访问互斥
 */ 
struct files_struct {
    struct inode *pwd;      
    struct file *fd_array;  
    int files_count;        
    semaphore_t files_sem;  
};

#define FILES_STRUCT_BUFSIZE                       (PGSIZE - sizeof(struct files_struct))
#define FILES_STRUCT_NENTRY                        (FILES_STRUCT_BUFSIZE / sizeof(struct file))
/**
 * 互斥锁
 */ 
void lock_files(struct files_struct *filesp);
void unlock_files(struct files_struct *filesp);

struct files_struct *files_create(void);
void files_destroy(struct files_struct *filesp);
void files_closeall(struct files_struct *filesp);
int dup_files(struct files_struct *to, struct files_struct *from);


/**
 * 返回访问此文件的线程数量
 */ 
static inline int
files_count(struct files_struct *filesp) {
    return filesp->files_count;
}

/**
 * 访问文件的进程数量+1
 * 返回当前访问此文件的线程数量
 */ 
static inline int
files_count_inc(struct files_struct *filesp) {
    filesp->files_count += 1;
    return filesp->files_count;
}
/**
 * 访问文件的进程数量-1
 * 返回当前访问此文件的线程数量
 */ 
static inline int
files_count_dec(struct files_struct *filesp) {
    filesp->files_count -= 1;
    return filesp->files_count;
}

#endif /* !__KERN_FS_FS_H__ */
