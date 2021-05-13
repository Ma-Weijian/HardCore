#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <vfs.h>
#include <inode.h>
#include <sem.h>
#include <kmalloc.h>
#include <error.h>

static semaphore_t bootfs_sem;
static struct inode *bootfs_node = NULL;

extern void vfs_devlist_init(void);

/**
 * @brief 给fs分配一块内存区域，被alloc_fs调用
 * 
 */
struct fs *
__alloc_fs(int type) {
    struct fs *fs;
    if ((fs = kmalloc(sizeof(struct fs))) != NULL) {
        fs->fs_type = type;
    }
    return fs;
}

/**
 * vfs初始化 
 */
void
vfs_init(void) {
    sem_init(&bootfs_sem, 1);
    vfs_devlist_init();
}

/**
 * 对根文件系加锁
 */
static void
lock_bootfs(void) {
    down(&bootfs_sem);
}

/**
 * 对根文件系统解锁
 */
static void
unlock_bootfs(void) {
    up(&bootfs_sem);
}

/*
 *设置根文件系统inode
 */
static void
change_bootfs(struct inode *node) {
    struct inode *old;
    lock_bootfs();
    {
        old = bootfs_node, bootfs_node = node;
    }
    unlock_bootfs();
    if (old != NULL) {
        vop_ref_dec(old);
    }
}

/**
 * 设置根文件系统目录
 */ 
int
vfs_set_bootfs(char *fsname) {
    struct inode *node = NULL;
    if (fsname != NULL) {
        char *s;
        if ((s = strchr(fsname, ':')) == NULL || s[1] != '\0') {
            return -E_INVAL;
        }
        int ret;
        if ((ret = vfs_chdir(fsname)) != 0) {
            return ret;
        }
        if ((ret = vfs_get_curdir(&node)) != 0) {
            return ret;
        }
    }
    /* 设置新的inode */
    change_bootfs(node);
    return 0;
}

/**
 * 返回根文件系统根节点的inode
 */
int
vfs_get_bootfs(struct inode **node_store) {
    struct inode *node = NULL;
    if (bootfs_node != NULL) {
        lock_bootfs();
        {
            if ((node = bootfs_node) != NULL) {
                vop_ref_inc(bootfs_node);
            }
        }
        unlock_bootfs();
    }
    if (node == NULL) {
        return -E_NOENT;
    }
    *node_store = node;
    return 0;
}

