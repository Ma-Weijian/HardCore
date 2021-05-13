#ifndef __KERN_FS_VFS_INODE_H__
#define __KERN_FS_VFS_INODE_H__

#include <defs.h>
#include <dev.h>
#include <sfs.h>
#include <atomic.h>
#include <assert.h>

struct stat;
struct iobuf;

/**
 * inodes是对文件的抽象
 * 
 * 
 * in_info 表示包含不同文件系统特定的inode信息
 * in_type inode所属的文件系统类型，和in_info对应
 * ref_count inode的引用计数
 * open_count 打开此inode对应文件的个数
 * in_fs 抽象文件系统,包含访问文件系统的函数指针
 * in_ops 抽象的inode操作，包含访问inode的函数指针
 */
struct inode {
    union {
        struct device __device_info;
        struct sfs_inode __sfs_inode_info;
    } in_info;
    enum {
        inode_type_device_info = 0x1234,
        inode_type_sfs_inode_info,
    } in_type;
    int ref_count;
    int open_count;
    struct fs *in_fs;
    const struct inode_ops *in_ops;
};

#define __in_type(type)                                             inode_type_##type##_info

#define check_inode_type(node, type)                                ((node)->in_type == __in_type(type))

#define __vop_info(node, type)                                      \
    ({                                                              \
        struct inode *__node = (node);                              \
        assert(__node != NULL && check_inode_type(__node, type));   \
        &(__node->in_info.__##type##_info);                         \
     })

#define vop_info(node, type)                                        __vop_info(node, type)

#define info2node(info, type)                                       \
    to_struct((info), struct inode, in_info.__##type##_info)

struct inode *__alloc_inode(int type);

#define alloc_inode(type)                                           __alloc_inode(__in_type(type))

#define MAX_INODE_COUNT                     0x10000

int inode_ref_inc(struct inode *node);
int inode_ref_dec(struct inode *node);
int inode_open_inc(struct inode *node);
int inode_open_dec(struct inode *node);

void inode_init(struct inode *node, const struct inode_ops *ops, struct fs *fs);
void inode_kill(struct inode *node);

#define VOP_MAGIC                           0x8c4ba476


/**
 * inode的抽象接口
 * 被宏 VOP_FOO(inode, args) 调用
 * 宏展开为 inode->inode_ops->vop_foo(inode, args). 
 * 这些宏函数如下
 *
 *    vop_open        - 调用文件系统的open()，可以使用不同的方式打开，包括
 *                      O_CREAT, O_EXCL，O_TRUNC，这些都是在vfs层处理的
 *                      
 *
 *    vop_close       - 当inode的打开计数为0是调用此函数将文件关闭
 *
 *    vop_reclaim     - 当inode的引用计数为0时释放inode的所有资源，基本上给是在
 *                      vfs_close之后调用的
 *****************************************
 *
 *    vop_read        - 读取文件数据到iob中，offset是iob的偏移，更新resid来反应
 *                      读了多少数据
 *                      不允许度目录或者软连接
 * 
 *    vop_getdirentry - 从iob中的目录下读取当前目录的文件名，同时offset增加，可以
 *                      一直遍历读出当前目录所有文件/子目录
 *
 *    vop_write       - 将uio中的数据写入文件
 *
 *    vop_ioctl       - 对设备进行io控制
 *
 *    vop_fstat       - 读取文件的fstat文件信息
 *
 *    vop_gettype     - 读取文件的type，具体见sfs.h.
 *
 *    vop_tryseek     - 检查文件位置指针偏移量 pos 是否在合法范围内
 *                          
 *    vop_fsync       - 将内存中修改过的文件可持久化到磁盘
 *
 *    vop_truncate    - 强行改变文件大小
 *
 *    vop_namefile    - 计算相对于文件文件系统根目录的路径名，并将其复制到指定的io缓冲区。
 *                      不需要在不是目录的对象上工作。
 *
 *****************************************
 *
 *    vop_create      - 创建一个文件，在目录dir下。如果excl为1，那如果文件则返回错误。
 *                      否则使用已存在的文件。
 *****************************************
 *
 *    vop_lookup      - 分析 PATHNAME 相关的目录,返回目录对应的inode节点
 */
struct inode_ops {
    unsigned long vop_magic;
    int (*vop_open)(struct inode *node, uint32_t open_flags);
    int (*vop_close)(struct inode *node);
    int (*vop_read)(struct inode *node, struct iobuf *iob);
    int (*vop_write)(struct inode *node, struct iobuf *iob);
    int (*vop_fstat)(struct inode *node, struct stat *stat);
    int (*vop_fsync)(struct inode *node);
    int (*vop_mkdir)(struct inode *node, const char *name);
    int (*vop_link)(struct inode *node, const char *name, struct inode *link_node);
    int (*vop_namefile)(struct inode *node, struct iobuf *iob);
    int (*vop_getdirentry)(struct inode *node, struct iobuf *iob);
    int (*vop_reclaim)(struct inode *node);
    int (*vop_gettype)(struct inode *node, uint32_t *type_store);
    int (*vop_tryseek)(struct inode *node, off_t pos);
    int (*vop_truncate)(struct inode *node, off_t len);
    int (*vop_create)(struct inode *node, const char *name, bool excl, struct inode **node_store);
    int (*vop_unlink)(struct inode *node, const char *name);
    int (*vop_lookup)(struct inode *node, char *path, struct inode **node_store);
    int (*vop_lookup_parent)(struct inode *node, char *path, struct inode **node_store, char **endp);
    int (*vop_ioctl)(struct inode *node, int op, void *data);
};

/*
 * 进行一致性检查，检查vop是否为NULL
 */
void inode_check(struct inode *node, const char *opstr);

#define __vop_op(node, sym)                                                                         \
    ({                                                                                              \
        struct inode *__node = (node);                                                              \
        assert(__node != NULL && __node->in_ops != NULL && __node->in_ops->vop_##sym != NULL);      \
        inode_check(__node, #sym);                                                                  \
        __node->in_ops->vop_##sym;                                                                  \
     })


#define vop_open(node, open_flags)                                  (__vop_op(node, open)(node, open_flags))
#define vop_close(node)                                             (__vop_op(node, close)(node))
#define vop_read(node, iob)                                         (__vop_op(node, read)(node, iob))
#define vop_write(node, iob)                                        (__vop_op(node, write)(node, iob))
#define vop_fstat(node, stat)                                       (__vop_op(node, fstat)(node, stat))
#define vop_fsync(node)                                             (__vop_op(node, fsync)(node))
#define vop_mkdir(node, name)                                       (__vop_op(node, mkdir)(node, name))
#define vop_link(node, name, link_node)                             (__vop_op(node, link)(node, name, link_node))
#define vop_namefile(node, iob)                                     (__vop_op(node, namefile)(node, iob))
#define vop_getdirentry(node, iob)                                  (__vop_op(node, getdirentry)(node, iob))
#define vop_reclaim(node)                                           (__vop_op(node, reclaim)(node))
#define vop_ioctl(node, op, data)                                   (__vop_op(node, ioctl)(node, op, data))
#define vop_gettype(node, type_store)                               (__vop_op(node, gettype)(node, type_store))
#define vop_tryseek(node, pos)                                      (__vop_op(node, tryseek)(node, pos))
#define vop_truncate(node, len)                                     (__vop_op(node, truncate)(node, len))
#define vop_create(node, name, excl, node_store)                    (__vop_op(node, create)(node, name, excl, node_store))
#define vop_unlink(node, name)                                      (__vop_op(node, unlink)(node, name))
#define vop_lookup(node, path, node_store)                          (__vop_op(node, lookup)(node, path, node_store))
#define vop_lookup_parent(node, path, node_store, endp)             (__vop_op(node, lookup_parent)(node, path, node_store, endp))


#define vop_fs(node)                                                ((node)->in_fs)
#define vop_init(node, ops, fs)                                     inode_init(node, ops, fs)
#define vop_kill(node)                                              inode_kill(node)

/*
 * 引用计数操作
 */
#define vop_ref_inc(node)                                           inode_ref_inc(node)
#define vop_ref_dec(node)                                           inode_ref_dec(node)

/**
 * 打开计数操作
 * vop_open_inc被vfs_open调用
 * vop_open_dec 被vop_close调用
 * 两者都不会被vfs以上的函数调用
 */
#define vop_open_inc(node)                                          inode_open_inc(node)
#define vop_open_dec(node)                                          inode_open_dec(node)

/**
 * @brief 返回当前inode引用计数node->ref_count
 * 
 * @param node 
 * @return int 
 */
static inline int
inode_ref_count(struct inode *node) {
    return node->ref_count;
}

/**
 * @brief 返回打开此inode对应文件个数node->open_count
 * 
 * @param node 
 * @return int 
 */
static inline int
inode_open_count(struct inode *node) {
    return node->open_count;
}

#endif /* !__KERN_FS_VFS_INODE_H__ */

