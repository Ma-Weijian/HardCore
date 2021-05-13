#ifndef __KERN_FS_VFS_VFS_H__
#define __KERN_FS_VFS_VFS_H__

#include <defs.h>
#include <fs.h>
#include <sfs.h>

struct inode;   // 对磁盘上的文件进行抽象的inode结构体 (inode.h)
struct device;  // 对设备相关数据结构 (dev.h)
struct iobuf;   // 读写用的结构体 (iobuf.h)



/**
 * @brief 抽象文件系统(设备可作为文件访问)
 * 信息：
 *      fs_info   : 具体的文件系统信息(目前只有sfs)
 *      fs_type   : 文件系统类型(目前只有sfs)
 * 操作: 
 */
struct fs {
    union {
        struct sfs_fs __sfs_info;                   
    } fs_info;                                     // 具体文件系统信息
    enum {
        fs_type_sfs_info,
    } fs_type;                                     // 文件系统类型
    int (*fs_sync)(struct fs *fs);                 // 将文件系统所有buffer可持久化到磁盘接口
    struct inode *(*fs_get_root)(struct fs *fs);   // 返回当前文件系统根目录接口
    int (*fs_unmount)(struct fs *fs);              // 尝试卸载文件系统接口
    void (*fs_cleanup)(struct fs *fs);             // 清除文件系统
};

#define __fs_type(type)                                             fs_type_##type##_info

#define check_fs_type(fs, type)                                     ((fs)->fs_type == __fs_type(type))

#define __fsop_info(_fs, type) ({                                   \
            struct fs *__fs = (_fs);                                \
            assert(__fs != NULL && check_fs_type(__fs, type));      \
            &(__fs->fs_info.__##type##_info);                       \
        })

#define fsop_info(fs, type)                 __fsop_info(fs, type)

#define info2fs(info, type)                                         \
    to_struct((info), struct fs, fs_info.__##type##_info)

struct fs *__alloc_fs(int type);

#define alloc_fs(type)                                              __alloc_fs(__fs_type(type))

// 石永红缩短调用长度
#define fsop_sync(fs)                       ((fs)->fs_sync(fs))
#define fsop_get_root(fs)                   ((fs)->fs_get_root(fs))
#define fsop_unmount(fs)                    ((fs)->fs_unmount(fs))
#define fsop_cleanup(fs)                    ((fs)->fs_cleanup(fs))


/**
 * 虚拟文件系统的系统函数
 * 
 * VFS层将对磁盘上的抽象文件或路径名的操作转换为对特定文件系统上的特定文件的操作。
 */


void vfs_init(void);
void vfs_cleanup(void);
void vfs_devlist_init(void);


/**
 * @brief VFS层底层接口
 * inode.h中放置了inode的相关操作
 * fs.h中存放的文件系统和设备的操作
 */

/**
 *    vfs_set_curdir   - 改变当前进程所在的目录
 *    vfs_get_curdir   - 返回当前进程所在目录对应的inode
 *    vfs_get_root     - get root inode for the filesystem named DEVNAME
 *    vfs_get_devname  - 得到文件系统对应设备名
 */
int vfs_set_curdir(struct inode *dir);
int vfs_get_curdir(struct inode **dir_store);
int vfs_get_root(const char *devname, struct inode **root_store);
const char *vfs_get_devname(struct fs *fs);


/**
 * VFS层对路径操作高层接口
 * 
 *    vfs_open         - 打开或者创建一个文件
 *    vfs_close        - 关闭一个vfs_open打开过的文件，不会失败
 *    vfs_link         - 创建一个硬链接
 *    vfs_symlink      - 创建一个符号链接
 *    vfs_readlink     - 读取一个 符号链接到iob中
 *    vfs_mkdir        - 创建一个目录
 *    vfs_unlink       - 删除一个连接
 *    vfs_rename       - 改变文件名称
 *    vfs_chdir        - 改变当前进程所在目录
 *    vfs_getcwd       - 返回当前进程所在目录名 
 */ 
int vfs_close(struct inode *node);
int vfs_open(char *path, uint32_t open_flags, struct inode **inode_store);
int vfs_link(char *old_path, char *new_path);
int vfs_symlink(char *old_path, char *new_path);
int vfs_readlink(char *path, struct iobuf *iob);
int vfs_mkdir(char *path);
int vfs_unlink(char *path);
int vfs_rename(char *old_path, char *new_path);
int vfs_chdir(char *path);
int vfs_getcwd(struct iobuf *iob);


/*
 * VFS layer mid-level operations.
 *
 *    vfs_lookup     - 通过路径获得对应目录/文件的inode
 * 
 *    vfs_lookparent - 获得设备/文件系统/相对路径根目录对应inode ,以及相对根目录的路径
 *                     这两种情况都可能破坏传入的路径。
 */
int vfs_lookup(char *path, struct inode **node_store);
int vfs_lookup_parent(char *path, struct inode **node_store, char **endp);

/*
 * Misc
 *
 *    vfs_set_bootfs - 设置以斜线开头的路径被发送到的文件系统。如果不设置，
 *                     这些路径会以 ENOENT 失败。参数应该是文件系统的设备
 *                     名或卷名（如 "lhd0:"），但不需要有尾部的冒号。
 *
 *    vfs_get_bootfs - 返回根文件系统的inode 
 *
 *    vfs_add_fs     - 在VFS命名的设备列表中添加一个文件系统。它将作为 "devname:"
 *                     被访问。这是为文件系统设备（如emufs）和小工具
 *                    （如Linux procfs或BSD kernfs）准备的，而不是为了在
 *                     磁盘设备上挂载文件系统。
 *
 *    vfs_add_dev    - 添加一个设备到VFS命名的设备列表。
 *
 *    vfs_mount      - vfs层挂载一个设备（初始化设备）
 *                     需要先调用vfs_do_add将设备加入设备链表
 *
 *    vfs_unmount    - 通过设备名称卸载filesystem /device 
 *
 *    vfs_unmountall - 卸载vfs层所有设备/文件系统
 */
int vfs_set_bootfs(char *fsname);
int vfs_get_bootfs(struct inode **node_store);

int vfs_add_fs(const char *devname, struct fs *fs);
int vfs_add_dev(const char *devname, struct inode *devnode, bool mountable);

int vfs_mount(const char *devname, int (*mountfunc)(struct device *dev, struct fs **fs_store));
int vfs_unmount(const char *devname);
int vfs_unmount_all(void);

#endif /* !__KERN_FS_VFS_VFS_H__ */

