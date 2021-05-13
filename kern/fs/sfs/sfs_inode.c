#include <defs.h>
#include <string.h>
#include <stdlib.h>
#include <list.h>
#include <stat.h>
#include <kmalloc.h>
#include <vfs.h>
#include <dev.h>
#include <sfs.h>
#include <inode.h>
#include <iobuf.h>
#include <bitmap.h>
#include <error.h>
#include <assert.h>

static const struct inode_ops sfs_node_dirops;  // 目录操作
static const struct inode_ops sfs_node_fileops; // 文件操作

/**
 * lock_sin - sfs_inode上锁函数
 */
static void lock_sin(struct sfs_inode *sin) 
{
    down(&(sin->sem));
}

/**
 * unlock_sin - sfs_inode解锁函数
 */
static void unlock_sin(struct sfs_inode *sin) 
{
    up(&(sin->sem));
}

/**
 * sfs_get_ops - 返回针对文件类型的不同操作函数集合
 */
static const struct inode_ops *sfs_get_ops(uint16_t type) 
{
    switch (type) {
    case SFS_TYPE_DIR:
        return &sfs_node_dirops;
    case SFS_TYPE_FILE:
        return &sfs_node_fileops;
    }
    panic("invalid file type %d.\n", type);
}

/**
 * sfs_hash_list - 通过哈希表快速找到指定块号的sfs_inode
 */
static list_entry_t *sfs_hash_list(struct sfs_fs *sfs, uint32_t ino) 
{
    return sfs->hash_list + sin_hashfn(ino);
}

/**
 * sfs_set_links - 将sfs_inode添加到链表和哈希表中
 */
static void sfs_set_links(struct sfs_fs *sfs, struct sfs_inode *sin) 
{
    list_add(&(sfs->inode_list), &(sin->inode_link));
    list_add(sfs_hash_list(sfs, sin->ino), &(sin->hash_link));
}

/**
 * sfs_remove_links - 将sfs_inode从链表和哈希表中删除
 */
static void sfs_remove_links(struct sfs_inode *sin) 
{
    list_del(&(sin->inode_link));
    list_del(&(sin->hash_link));
}

/**
 * sfs_block_inuse - 查询某个块是否被使用
 */
static bool sfs_block_inuse(struct sfs_fs *sfs, uint32_t ino) 
{
    if (ino != 0 && ino < sfs->super.blocks) {
        return !bitmap_test(sfs->freemap, ino);
    }
    panic("sfs_block_inuse: called out of range (0, %u) %u.\n", sfs->super.blocks, ino);
}

/**
 * sfs_super_sync - 将sfs的超级块super和空闲位图freemap同步到磁盘中
 */
static int sfs_super_sync(struct sfs_fs *sfs) 
{
    int ret;
    if (sfs->super_dirty) {
        sfs->super_dirty = 0;
        if ((ret = sfs_sync_super(sfs)) != 0) {
            sfs->super_dirty = 1;
            return ret;
        }
        if ((ret = sfs_sync_freemap(sfs)) != 0) {
            sfs->super_dirty = 1;
            return ret;
        }
    }
}

/**
 * sfs_block_alloc - 分配一个空闲块, 可用于inode或数据, 返回空闲块的块号
 */
static int sfs_block_alloc(struct sfs_fs *sfs, uint32_t *ino_store) 
{
    int ret;
    if ((ret = bitmap_alloc(sfs->freemap, ino_store)) != 0) {
        return ret;
    }
    assert(sfs->super.unused_blocks > 0);
    sfs->super.unused_blocks --, sfs->super_dirty = 1;
    sfs_super_sync(sfs);
    assert(sfs_block_inuse(sfs, *ino_store));
    return sfs_clear_block(sfs, *ino_store, 1);
}

/**
 * sfs_block_free - 释放一个块
 */
static void sfs_block_free(struct sfs_fs *sfs, uint32_t ino) 
{
    assert(sfs_block_inuse(sfs, ino));
    bitmap_free(sfs->freemap, ino);
    sfs->super.unused_blocks ++, sfs->super_dirty = 1;
    sfs_super_sync(sfs);
}

/**
 * sfs_create_inode - 根据磁盘上的inode和块号, 创建一个vfs层的通用inode, 并完成初始化
 * @sfs:        sfs文件系统
 * @din:        读入的磁盘inode
 * @ino:        磁盘inode的块号
 * @node_store: 返回创建的通用inode
 */
static int sfs_create_inode(struct sfs_fs *sfs, struct sfs_disk_inode *din, uint32_t ino, struct inode **node_store) 
{
    struct inode *node;
    if ((node = alloc_inode(sfs_inode)) != NULL) {
        vop_init(node, sfs_get_ops(din->type), info2fs(sfs, sfs));
        struct sfs_inode *sin = vop_info(node, sfs_inode);
        sin->din = din, sin->ino = ino, sin->dirty = 0, sin->reclaim_count = 1;
        sem_init(&(sin->sem), 1);
        *node_store = node;
        return 0;
    }
    return -E_NO_MEM;
}

/**
 * lookup_sfs_nolock - 根据块号, 在内存中查找sfs_inode, 进而得到vfs通用inode
 * @sfs: sfs文件系统
 * @ino: 块号
 * NOTICE: inode引用次数+1
 */
static struct inode *lookup_sfs_nolock(struct sfs_fs *sfs, uint32_t ino) 
{
    struct inode *node;
    // 根据块号查找sfs_inode
    list_entry_t *list = sfs_hash_list(sfs, ino), *le = list;
    while ((le = list_next(le)) != list) {
        struct sfs_inode *sin = le2sin(le, hash_link);
        if (sin->ino == ino) {
            // 根据sfs_ino得到vfs通用inode
            node = info2node(sin, sfs_inode);
            if (vop_ref_inc(node) == 1) {
                sin->reclaim_count ++;
            }
            return node;
        }
    }
    return NULL;
}

/**
 * sfs_load_inode - 根据块号, 从磁盘中将inode载入到内存中
 *                - 如果inode已存在, 就直接返回对应的inode
 *                - 如果inode不存在, 首先在内存中创建disk_inode的空间, 然后从磁盘读disk_inode, 最后创建inode并加入链表中
 * @sfs:        sfs文件系统
 * @node_store: 返回块号对应的通用inode
 * @ino:        块号
 */
int sfs_load_inode(struct sfs_fs *sfs, struct inode **node_store, uint32_t ino) 
{
    lock_sfs_fs(sfs);
    // 尝试在内存中查找inode, 如果已查找到, 就直接存储到node_store中返回
    struct inode *node;
    if ((node = lookup_sfs_nolock(sfs, ino)) != NULL) {
        goto out_unlock;
    }

    // 未在内存中找到对应inode
    // 申请sfs_disk_inode的空间
    int ret = -E_NO_MEM;
    struct sfs_disk_inode *din;
    if ((din = kmalloc(sizeof(struct sfs_disk_inode))) == NULL) {
        goto failed_unlock;
    }

    assert(sfs_block_inuse(sfs, ino));
    // 从磁盘读入对应的块到sfs_disk_inode空间内
    if ((ret = sfs_rbuf(sfs, din, sizeof(struct sfs_disk_inode), ino, 0)) != 0) {
        goto failed_cleanup_din;
    }

    assert(din->nlinks != 0);
    // 创建vfs通用inode
    if ((ret = sfs_create_inode(sfs, din, ino, &node)) != 0) {
        goto failed_cleanup_din;
    }
    sfs_set_links(sfs, vop_info(node, sfs_inode));

out_unlock:
    unlock_sfs_fs(sfs);
    *node_store = node;
    return 0;

failed_cleanup_din:
    kfree(din);
failed_unlock:
    unlock_sfs_fs(sfs);
    return ret;
}

/**
 * sfs_bmap_get_sub_nolock - 根据一级索引块磁盘块号entp和索引项的索引值index找到对应的索引项(索引项内容即磁盘块号)
 *                         - 如果create为1, 则: 当索引块不存在时, 建立该索引块; 当对应的索引项未分配索引块时, 分配一个索引块
 * @sfs:        sfs文件系统
 * @entp:       索引块的磁盘块号
 * @index:      索引块内索引项的编号
 * @create:     当某索引项不存在时, 是否创建该索引项
 * @ino_store:  返回得到的索引项内容(即磁盘块号)
 */
static int sfs_bmap_get_sub_nolock(struct sfs_fs *sfs, uint32_t *entp, uint32_t index, bool create, uint32_t *ino_store) 
{
    assert(index < SFS_BLK_NENTRY);
    int ret;
    uint32_t ent, ino = 0;
    // 根据index求出目标索引项在块内的偏移
    off_t offset = index * sizeof(uint32_t); 
	// 如果对应的索引块存在, 就读索引块以offest为偏移处的索引项
    if ((ent = *entp) != 0) {
        if ((ret = sfs_rbuf(sfs, &ino, sizeof(uint32_t), ent, offset)) != 0) {
            return ret;
        }
        // 如果读出的索引项有效, 就直接返回对应的索引项
        // 如果读出的索引项无效, 但是create为0, 意味着不需要创建新索引项, 直接返回无效的索引项
        if (ino != 0 || !create) {
            goto out;
        }
    }
    else {
        // 对应索引块不存在, 但是create为0, 直接返回无效的索引项
        if (!create) {
            goto out;
        }
		// 对应索引块不存在, 而且create为1, 先分配一个索引块
        if ((ret = sfs_block_alloc(sfs, &ent)) != 0) {
            return ret;
        }
    }
    
    // 索引块已经存在(如果索引块不存在, 也已经创建完毕), 且create为1, 就为无效的索引项申请一个块, 将块号填入索引项使其变为有效的索引块
    if ((ret = sfs_block_alloc(sfs, &ino)) != 0) {
        goto failed_cleanup;
    }
    if ((ret = sfs_wbuf(sfs, &ino, sizeof(uint32_t), ent, offset)) != 0) {
        sfs_block_free(sfs, ino);
        goto failed_cleanup;
    }

out:
    if (ent != *entp) {
        *entp = ent;
    }
    *ino_store = ino;
    return 0;

failed_cleanup:
    if (ent != *entp) {
        sfs_block_free(sfs, ent);
    }
    return ret;
}

/**
 * sfs_bmap_get_nolock - 从sfs_inode中根据index直接得到对应的块号, 屏蔽直接块和间接块的差异
 * @sfs:        sfs文件系统
 * @sin:        文件inode
 * @index:      文件inode中的块索引号, 范围是(0~11+1024)
 * @create:     当对应块没有被分配时, 如果create=1, 就创建该块; 否则, 什么都不做
 * @ino_store:  根据index得到的块号
 */
static int
sfs_bmap_get_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t index, bool create, uint32_t *ino_store) 
{
    struct sfs_disk_inode *din = sin->din;
    int ret;
    uint32_t ent, ino;
	// 如果index值在0~11之间, 表明该块是直接块, 对应的磁盘块号可以直接从direct数组中取得
    if (index < SFS_NDIRECT) {
        if ((ino = din->direct[index]) == 0 && create) {
            if ((ret = sfs_block_alloc(sfs, &ino)) != 0) {
                return ret;
            }
            din->direct[index] = ino;
            sin->dirty = 1;
        }
        goto out;
    }
    // 如果index值超过了直接块的范围, 表明该块是一级间接块, 对应的磁盘块号可以在一级索引块中得到
    index -= SFS_NDIRECT;
    if (index < SFS_BLK_NENTRY) {
        ent = din->indirect;
        if ((ret = sfs_bmap_get_sub_nolock(sfs, &ent, index, create, &ino)) != 0) {
            return ret;
        }
        if (ent != din->indirect) {
            assert(din->indirect == 0);
            din->indirect = ent;
            sin->dirty = 1;
        }
        goto out;
    } else {
		panic ("sfs_bmap_get_nolock - index out of range");
	}
out:
    assert(ino == 0 || sfs_block_inuse(sfs, ino));
    *ino_store = ino;
    return 0;
}

/**
 * sfs_bmap_free_sub_nolock - 将索引块中的对应索引项置为0, 并释放对应的块
 * @sfs:    sfs文件系统
 * @ent:    索引块的块号
 * @index:  索引块内索引项的编号
 */
static int sfs_bmap_free_sub_nolock(struct sfs_fs *sfs, uint32_t ent, uint32_t index) 
{
    assert(sfs_block_inuse(sfs, ent) && index < SFS_BLK_NENTRY);
    int ret;
    uint32_t ino, zero = 0;
    off_t offset = index * sizeof(uint32_t);
    if ((ret = sfs_rbuf(sfs, &ino, sizeof(uint32_t), ent, offset)) != 0) {
        return ret;
    }
    if (ino != 0) {
        if ((ret = sfs_wbuf(sfs, &zero, sizeof(uint32_t), ent, offset)) != 0) {
            return ret;
        }
        sfs_block_free(sfs, ino);
    }
    return 0;
}

/**
 * sfs_bmap_free_nolock - 从sfs_inode中根据index释放对应的块, 屏蔽直接块和间接块的差异
 * @sfs:    sfs文件系统
 * @sin:    文件inode
 * @index:  文件inode中的块索引号
 */
static int sfs_bmap_free_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t index) 
{
    struct sfs_disk_inode *din = sin->din;
    int ret;
    uint32_t ent, ino;
    if (index < SFS_NDIRECT) {
        if ((ino = din->direct[index]) != 0) {
			// free the block
            sfs_block_free(sfs, ino);
            din->direct[index] = 0;
            sin->dirty = 1;
        }
        return 0;
    }

    index -= SFS_NDIRECT;
    if (index < SFS_BLK_NENTRY) {
        if ((ent = din->indirect) != 0) {
			// set the entry item to 0 in the indirect block
            if ((ret = sfs_bmap_free_sub_nolock(sfs, ent, index)) != 0) {
                return ret;
            }
        }
        return 0;
    }
    return 0;
}

/**
 * sfs_bmap_load_nolock - 从文件inode中根据index得到对应的块号, 如果index为该文件的未分配的下一个数据块, 就创建该数据块
 * @sfs:        sfs文件系统
 * @sin:        文件inode
 * @index:      目标块在文件中的索引号
 * @ino_store:  返回的块号
 */
static int sfs_bmap_load_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t index, uint32_t *ino_store) 
{
    struct sfs_disk_inode *din = sin->din;
    assert(index <= din->blocks);
    int ret;
    uint32_t ino;
    bool create = (index == din->blocks);
    if ((ret = sfs_bmap_get_nolock(sfs, sin, index, create, &ino)) != 0) {
        return ret;
    }
    assert(sfs_block_inuse(sfs, ino));
    if (create) {
        din->blocks ++;
    }
    if (ino_store != NULL) {
        *ino_store = ino;
    }
    return 0;
}

/**
 * sfs_bmap_truncate_nolock - 释放inode中指向的最后一个数据块
 * @sfs:    sfs文件系统
 * @sin:    文件inode
 */
static int sfs_bmap_truncate_nolock(struct sfs_fs *sfs, struct sfs_inode *sin) 
{
    struct sfs_disk_inode *din = sin->din;
    assert(din->blocks != 0);
    int ret;
    if ((ret = sfs_bmap_free_nolock(sfs, sin, din->blocks - 1)) != 0) {
        return ret;
    }
    din->blocks --;
    sin->dirty = 1;
    return 0;
}

/**
 * sfs_dirent_read_nolock - 从inode指向的磁盘块中读目录项, 放到entry对应的地址
 * @sfs:    sfs文件系统
 * @sin:    目录文件inode
 * @slot:   目录项在目录文件中的索引号
 * @entry:  文件目录项目标地址
 * NOTICE: 为了简化实现, 每个块只存放一个目录项
 **/
static int sfs_dirent_read_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, int slot, struct sfs_disk_entry *entry) 
{
    assert(sin->din->type == SFS_TYPE_DIR && (slot >= 0 && slot < sin->din->blocks));
    int ret;
    uint32_t ino;
    // 根据目录inode和目录项的索引号, 找到目录项所在磁盘块的块号
    if ((ret = sfs_bmap_load_nolock(sfs, sin, slot, &ino)) != 0) {
        return ret;
    }
    assert(sfs_block_inuse(sfs, ino));
    // 从磁盘块中读取目录项
    if ((ret = sfs_rbuf(sfs, entry, sizeof(struct sfs_disk_entry), ino, 0)) != 0) {
        return ret;
    }
    entry->name[SFS_MAX_FNAME_LEN] = '\0';
    return 0;
}

/**
 * sfs_dirent_link_nolock - 将目录项链接到一个inode上
 * @sfs:    sfs文件系统
 * @sin:    父目录的inode
 * @slot:   目录项在父目录中的索引值
 * @lnksin: 被链接的inode
 * @name:   目录项中的名字
 */
static int sfs_dirent_link_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, int slot, struct sfs_inode *lnksin, const char *name) 
{
    int ret;
    uint32_t ino;
    // 找到slot对应目录项的ino, 注意该目录项应该为空
    if ((ret = sfs_bmap_load_nolock(sfs, sin, slot, &ino)) != 0) {
        return ret;
    }
    assert(sfs_block_inuse(sfs, ino));
    // 写新目录项到ino对应的块
    if ((ret = sfs_clear_block(sfs, ino, 1)) != 0) {
        return ret;
    }
    struct sfs_disk_entry *entry;
    if ((entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL) {
        return -E_NO_MEM;
    }
    memset(entry, 0, sizeof(struct sfs_disk_entry));
    entry->ino = lnksin->ino;
    strncpy(entry->name, name, SFS_MAX_FNAME_LEN);
    if ((ret = sfs_wbuf(sfs, entry, sizeof(struct sfs_disk_entry), ino, 0)) != 0) {
        kfree(entry);
        return ret;
    }
    // 更新父目录文件大小信息
    sin->din->size += (SFS_MAX_FNAME_LEN + 1);
    // 更新被链接inode的链接数
    lnksin->dirty = 1;
    lnksin->din->nlinks ++;
    sin->dirty = 1;
    kfree(entry);
    return 0;
}

/**
 * sfs_dirent_unlink_nolock - 取消一个目录项到一个inode的链接, 如果没有目录项链接到该文件, 就删除该文件
 * @sfs: sfs文件系统
 * @sin: 父目录的inode
 * @slot: 目录项在父目录中的索引值
 * @lnksin: 被链接的文件的inode
 */
static int sfs_dirent_unlink_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, int slot, struct sfs_inode *lnksin)
{
    int ret;
    uint32_t ino;
    struct sfs_disk_inode *din = sin->din;
    struct sfs_disk_inode *lnkdin = lnksin->din;
    
    // 读该目录项的块号
    if ((ret = sfs_bmap_get_nolock(sfs, sin, slot, 0, &ino)) != 0) {
        return ret;
    }
    // 读该目录项的内容
    ret = -E_NO_MEM;
    struct sfs_disk_entry *entry;
    if ((entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL)
        return -E_NO_MEM;
    if ((ret = sfs_rbuf(sfs, entry, sizeof(struct sfs_disk_entry), ino, 0)) != 0) {
        goto failed_cleanup;
    }
    // 检查目录项是否为..和.
    if (strncmp(entry->name, "..", SFS_MAX_FNAME_LEN) == 0 || strncmp(entry->name, ".", SFS_MAX_FNAME_LEN) == 0) {
        ret = -E_NOTEMPTY;
        goto failed_cleanup;
    }
    // 检查目录项指向的inode与目标inode是否相同
    if (entry->ino != lnksin->ino)
        goto failed_cleanup;
    // 清空目录项
    if ((ret = sfs_clear_block(sfs, ino, 1)) != 0) {
        return ret;
    }
    // 更新父目录的文件大小信息
    din->size -= (SFS_MAX_FNAME_LEN + 1);
    // 更新链接数
    lnkdin->nlinks --;
    // 如果是被链接的文件是目录文件, 父目录的硬链接数减1(".."目录项)
    if(S_ISDIR(lnkdin->type)) {
        din->nlinks --;
    }
    lnksin->dirty = 1;
    sin->dirty = 1;

    kfree(entry);
    return 0;

failed_cleanup:
    kfree(entry);
    return ret;
}

/**
 * sfs_dirent_search_nolock - 读目录文件中的每个目录项, 并检查与目标文件名是否匹配
 *                            如果匹配到目标文件名, 则返回slot(目录项在目录文件中的索引), ino(目录项指向的磁盘块), 
 *                            无论是否匹配到目标文件名, 都会返回empty_slot(目录文件中的无效目录项的索引号)
 * @sfs:        sfs文件系统
 * @sin:        目录文件inode
 * @name:       搜索的文件名
 * @ino_store:  找到的目录项指向的磁盘块号
 * @slot:       目录项在目录文件中的索引号
 * @empty_slot: 目录文件中的无效目录项的索引号(用于创建新目录项)
 */
static int sfs_dirent_search_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, const char *name, uint32_t *ino_store, int *slot, int *empty_slot) 
{
    assert(strlen(name) <= SFS_MAX_FNAME_LEN);
    // 申请目录项的空间
    struct sfs_disk_entry *entry;
    if ((entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL) {
        return -E_NO_MEM;
    }

#define set_pvalue(x, v)            do { if ((x) != NULL) { *(x) = (v); } } while (0)
    int ret, i, nslots = sin->din->blocks;
    // 默认的无效目录项的索引号指向最后一个目录项
    set_pvalue(empty_slot, nslots);
    for (i = 0; i < nslots; i ++) {
        // 读取目录项
        if ((ret = sfs_dirent_read_nolock(sfs, sin, i, entry)) != 0) {
            goto out;
        }
        // 如果该目录项指向一个无效块, 则表示该目录项是一个无效目录项, 更新empty_slot的值
        if (entry->ino == 0) {
            set_pvalue(empty_slot, i);
            continue ;
        }
        // 如果匹配到目标文件名, 则将设置slot值和ino_store
        if (strcmp(name, entry->name) == 0) {
            set_pvalue(slot, i);
            set_pvalue(ino_store, entry->ino);
            goto out;
        }
    }
#undef set_pvalue
    ret = -E_NOENT;
out:
    kfree(entry);
    return ret;
}

/**
 * sfs_dirent_findino_nolock - 读取目录文件中的每个目录项, 并检查与目标块号是否匹配
 *                             存在匹配则返回0, 否则返回-E_NOENT
 * @sfs:    sfs文件系统
 * @sin:    目录文件inode
 * @ino:    目标块号
 * @entry:  返回读取到的目录项
 */
static int sfs_dirent_findino_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t ino, struct sfs_disk_entry *entry) 
{
    int ret, i, nslots = sin->din->blocks;
    for (i = 0; i < nslots; i ++) {
        if ((ret = sfs_dirent_read_nolock(sfs, sin, i, entry)) != 0) {
            return ret;
        }
        if (entry->ino == ino) {
            return 0;
        }
    }
    return -E_NOENT;
}

/**
 * sfs_lookup_once - 在目录下根据文件名查找对应的文件
 * @sfs:        sfs文件系统
 * @sin:        目录文件inode
 * @name:       目标文件的文件名
 * @node_store: 查找到的目标文件的通用inode
 * @slot:       找到的目录项在目录文件中的索引号
 */
static int sfs_lookup_once(struct sfs_fs *sfs, struct sfs_inode *sin, const char *name, struct inode **node_store, int *slot) 
{
    int ret;
    uint32_t ino;
    lock_sin(sin);
    {   // 根据名字搜索目录, 得到目录项的索引号和目录项指向的磁盘块号
        ret = sfs_dirent_search_nolock(sfs, sin, name, &ino, slot, NULL);
    }
    unlock_sin(sin);
    if (ret == 0) {
		// 从磁盘块中读入inode到node_store
        ret = sfs_load_inode(sfs, node_store, ino);
    }
    return ret;
}

/**
 * sfs_opendir - 针对目录文件检查打开文件标志, 限制目录文件只能采用只读方式打开
 * @node: 目录文件的通用inode
 * @open_flags: 打开文件标志
 */
static int sfs_opendir(struct inode *node, uint32_t open_flags) 
{
    switch (open_flags & O_ACCMODE) {
    case O_RDONLY:
        break;
    case O_WRONLY:
    case O_RDWR:
    default:
        return -E_ISDIR;
    }
    if (open_flags & O_APPEND) {
        return -E_ISDIR;
    }
    return 0;
}

/**
 * sfs_openfile - 针对常规文件检查打开文件标志, 不检查
 */
static int sfs_openfile(struct inode *node, uint32_t open_flags) 
{
    return 0;
}

/**
 * sfs_close - 关闭文件, 同步文件到磁盘
 */
static int sfs_close(struct inode *node) 
{
    return vop_fsync(node);
}

/**
 * sfs_link - 将文件名链接到一个文件的inode
 * @node: 目录文件通用inode, 这里是父目录
 * @name: 文件名
 * @link_node: 文件通用inode, 这里是被链接的文件
 */
static int sfs_link(struct inode *node, const char *name, struct inode *link_node) 
{
    int ret;
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    struct sfs_inode *lnksin = vop_info(link_node, sfs_inode);
    uint32_t ino;
    int empty_slot;

    lock_sin(sin);
    // 在父目录下搜索该名字的文件, 搜索不到才能创建
    if ((ret = sfs_dirent_search_nolock(sfs, sin, name, &ino, NULL, &empty_slot)) != -E_NOENT) {
        unlock_sin(sin);
        return (ret == 0) ? -E_EXISTS : ret;
    }
    // 将文件名name链接到link_node
    if ((ret = sfs_dirent_link_nolock(sfs, sin, empty_slot, lnksin, name)) != 0) {
        unlock_sin(sin);
        return ret;
    }
    unlock_sin(sin);

    return 0;
}

/**
 * sfs_unlink - 取消一个文件名的链接
 * @node: 目录文件通用inode, 这里是父目录
 * @name: 被取消链接的文件名
 */
static int sfs_unlink(struct inode *node, const char *name)
{
    int ret;
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int slot;
    uint32_t ino;

    lock_sin(sin);
    // 在父目录下搜索该文件名, 得到目录项指向的块号
    if ((ret = sfs_dirent_search_nolock(sfs, sin, name, &ino, &slot, NULL)) != 0) {
        goto failed_unlock;
    }
    // 读取目录项指向的文件inode
    struct inode *link_node;
    if ((ret = sfs_load_inode(sfs, &link_node, ino)) != 0) {
        goto failed_unlock;
    }
    // 取消文件名目录项到文件inode的链接
    struct sfs_inode *lnksin = vop_info(link_node, sfs_inode);
    if ((ret = sfs_dirent_unlink_nolock(sfs, sin, slot, lnksin)) != 0) {
        goto failed_unlock;
    }
    unlock_sin(sin);
    return 0;

failed_unlock:
    unlock_sin(sin);
    return ret;
}

/**
 * sfs_io_nolock - 从文件的offset位置开始读写文件内容, 读写长度为alenp
 * @sfs:      sfs文件系统
 * @sin:      文件inode
 * @buf:      读写缓冲区
 * @offset:   文件offset, 即读写开始的位置
 * @alenp:    传入需要读写的长度, 传出实际读写的长度
 * @write:    0表示读, 1表示写
 */
static int sfs_io_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, void *buf, off_t offset, size_t *alenp, bool write) 
{
    struct sfs_disk_inode *din = sin->din;
    assert(din->type != SFS_TYPE_DIR);
    off_t endpos = offset + *alenp, blkoff;
    *alenp = 0;
	// 计算读写的终止位置endpos
    if (offset < 0 || offset >= SFS_MAX_FILE_SIZE || offset > endpos) {
        return -E_INVAL;
    }
    if (offset == endpos) {
        return 0;
    }
    if (endpos > SFS_MAX_FILE_SIZE) {
        endpos = SFS_MAX_FILE_SIZE;
    }
    if (!write) {
        if (offset >= din->size) {
            return 0;
        }
        if (endpos > din->size) {
            endpos = din->size;
        }
    }

    int (*sfs_buf_op)(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset);
    int (*sfs_block_op)(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks);
    if (write) {
        sfs_buf_op = sfs_wbuf, sfs_block_op = sfs_wblock;
    }
    else {
        sfs_buf_op = sfs_rbuf, sfs_block_op = sfs_rblock;
    }

    int ret = 0;
    size_t size, alen = 0;
    uint32_t ino;
    uint32_t blkno = offset / SFS_BLKSIZE;          
    uint32_t nblks = endpos / SFS_BLKSIZE - blkno; 
    // 先读写一段数据, 保证后续读写可以与数据块对齐
    if ((blkoff = offset % SFS_BLKSIZE) != 0) {
        size = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset);
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        if ((ret = sfs_buf_op(sfs, buf, size, ino, blkoff)) != 0) {
            goto out;
        }
        alen += size;
        if (nblks == 0) {
            goto out;
        }
        buf += size, blkno ++, nblks --;
    }
    // 持续按块读写
    size = SFS_BLKSIZE;
    while (nblks != 0) {
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        if ((ret = sfs_block_op(sfs, buf, ino, 1)) != 0) {
            goto out;
        }
        alen += size, buf += size, blkno ++, nblks --;
    }
    // 读写剩余不足一块的数据
    if ((size = endpos % SFS_BLKSIZE) != 0) {
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        if ((ret = sfs_buf_op(sfs, buf, size, ino, 0)) != 0) {
            goto out;
        }
        alen += size;
    }
out:
    *alenp = alen;
    if (offset + alen > sin->din->size) {
        sin->din->size = offset + alen;
        sin->dirty = 1;
    }
    return ret;
}

/**
 * sfs_io - sfs_io_nolock函数的加锁版本, 读写成功会移动iobuf的指针
 * @node:   vfs层通用inode
 * @iob:    数据传输通用缓冲区
 * @write:  0表示读, 1表示写
 */
static inline int sfs_io(struct inode *node, struct iobuf *iob, bool write) 
{
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int ret;
    lock_sin(sin);
    {
        size_t alen = iob->io_resid;
        ret = sfs_io_nolock(sfs, sin, iob->io_base, iob->io_offset, &alen, write);
        if (alen != 0) {
            iobuf_skip(iob, alen);
        }
    }
    unlock_sin(sin);
    return ret;
}

// sfs_read - 读文件, 调用sfs_io进行读操作
static int sfs_read(struct inode *node, struct iobuf *iob) 
{
    return sfs_io(node, iob, 0);
}

// sfs_write - 调用sfs_io进行写操作
static int sfs_write(struct inode *node, struct iobuf *iob) 
{
    return sfs_io(node, iob, 1);
}

/**
 * sfs_fstat - 得到文件的块数, 硬链接数, 大小等信息
 * @node: vfs层通用inode
 * @stat: 统计信息
 */
static int sfs_fstat(struct inode *node, struct stat *stat) 
{
    int ret;
    memset(stat, 0, sizeof(struct stat));
    if ((ret = vop_gettype(node, &(stat->st_mode))) != 0) {
        return ret;
    }
    struct sfs_disk_inode *din = vop_info(node, sfs_inode)->din;
    stat->st_nlinks = din->nlinks;
    stat->st_blocks = din->blocks;
    stat->st_size = din->size;
    return 0;
}

/**
 * sfs_fsync - 将内存中的一个inode的数据块同步到磁盘中
 */
static int sfs_fsync(struct inode *node) 
{
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int ret = 0;
    if (sin->dirty) {
        lock_sin(sin);
        {
            if (sin->dirty) {
                sin->dirty = 0;
                if ((ret = sfs_wbuf(sfs, sin->din, sizeof(struct sfs_disk_inode), sin->ino, 0)) != 0) {
                    sin->dirty = 1;
                }
            }
        }
        unlock_sin(sin);
    }
    return ret;
}

/**
 * sfs_namefile - 计算文件相对于根目录的路径, 并拷贝到缓冲区内
 */
static int sfs_namefile(struct inode *node, struct iobuf *iob) 
{
    struct sfs_disk_entry *entry;
    if (iob->io_resid <= 2 || (entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL) {
        return -E_NO_MEM;
    }

    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);

    int ret;
    char *ptr = iob->io_base + iob->io_resid;
    size_t alen, resid = iob->io_resid - 2;
    vop_ref_inc(node);
    while (1) {
        struct inode *parent;
        /* 找到当前目录inode的上层目录inode */
        if ((ret = sfs_lookup_once(sfs, sin, "..", &parent, NULL)) != 0) {
            goto failed;
        }

        uint32_t ino = sin->ino;
        vop_ref_dec(node);
        if (node == parent) {
            // 当前节点和父目录节点相同, 证明已到达根目录
            vop_ref_dec(node);
            break;
        }

        node = parent, sin = vop_info(node, sfs_inode);
        assert(ino != sin->ino && sin->din->type == SFS_TYPE_DIR);

        lock_sin(sin);
        {
            // 此时ino的值为当前目录的inode的块号, sin为父目录的inode, 直接根据ino在父目录中查找得到该项对应的目录项, 就是当前目录的目录项
            ret = sfs_dirent_findino_nolock(sfs, sin, ino, entry);
        }
        unlock_sin(sin);

        if (ret != 0) {
            goto failed;
        }

        if ((alen = strlen(entry->name) + 1) > resid) {
            goto failed_nomem;
        }
        resid -= alen, ptr -= alen;
        memcpy(ptr, entry->name, alen - 1);
        ptr[alen - 1] = '/';
    }
    alen = iob->io_resid - resid - 2;
    ptr = memmove(iob->io_base + 1, ptr, alen);
    ptr[-1] = '/', ptr[alen] = '\0';
    iobuf_skip(iob, alen);
    kfree(entry);
    return 0;

failed_nomem:
    ret = -E_NO_MEM;
failed:
    vop_ref_dec(node);
    kfree(entry);
    return ret;
}

/**
 * sfs_getdirentry_sub_noblock - 依次读文件中的每一个有效目录项, 直到找到对应slot的目录项
 * @sfs:    sfs文件系统
 * @sin:    目录文件的inode
 * @slot:   目标目录项在目录文件中的索引号
 * @entry:  返回有效目录项
 * NOTICE: 这里的slot是有效目录项的索引号, 无效目录项(ino==0)会被略过
 */
static int sfs_getdirentry_sub_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, int slot, struct sfs_disk_entry *entry) 
{
    int ret, i, nslots = sin->din->blocks;
    for (i = 0; i < nslots; i ++) {
        if ((ret = sfs_dirent_read_nolock(sfs, sin, i, entry)) != 0) {
            return ret;
        }
        if (entry->ino != 0) {
            if (slot == 0) {
                return 0;
            }
            slot --;
        }
    }
    return -E_NOENT;
}

/**
 * sfs_getdirentry - 根据iobuf的偏移量, 计算出目录项的slot, 然后得到目录项的内容(文件名)并写入iobuf
 * NOTICE: 调用了sfs_getdirentry_sub_nolock, 因此slot也是排除了无效目录项的索引值
 */
static int sfs_getdirentry(struct inode *node, struct iobuf *iob) 
{
    struct sfs_disk_entry *entry;
    if ((entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL) {
        return -E_NO_MEM;
    }

    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);

    int ret, slot;
    off_t offset = iob->io_offset;
    if (offset < 0 || offset % sfs_dentry_size != 0) {
        kfree(entry);
        return -E_INVAL;
    }
    if ((slot = offset / sfs_dentry_size) > sin->din->blocks) {
        kfree(entry);
        return -E_NOENT;
    }
    lock_sin(sin);
    if ((ret = sfs_getdirentry_sub_nolock(sfs, sin, slot, entry)) != 0) {
        unlock_sin(sin);
        goto out;
    }
    unlock_sin(sin);
    ret = iobuf_move(iob, entry->name, sfs_dentry_size, 1, NULL);
out:
    kfree(entry);
    return ret;
}

/**
 * sfs_reclaim - 释放该inode占据的所有资源, 表示这个inode不再被使用
 */
static int sfs_reclaim(struct inode *node) 
{
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);

    int  ret = -E_BUSY;
    uint32_t ent;
    lock_sfs_fs(sfs);
    assert(sin->reclaim_count > 0);
    if ((-- sin->reclaim_count) != 0 || inode_ref_count(node) != 0) {
        goto failed_unlock;
    }
    // 只有在该文件硬链接数为0的情况下才会回收该inode的资源
    if (sin->din->nlinks == 0) {
        if ((ret = vop_truncate(node, 0)) != 0) {
            goto failed_unlock;
        }
    }
    if (sin->dirty) {
        if ((ret = vop_fsync(node)) != 0) {
            goto failed_unlock;
        }
    }
    sfs_remove_links(sin);
    unlock_sfs_fs(sfs);

    if (sin->din->nlinks == 0) {
        sfs_block_free(sfs, sin->ino);
        if ((ent = sin->din->indirect) != 0) {
            sfs_block_free(sfs, ent);
        }
    }
    kfree(sin->din);
    vop_kill(node);
    return 0;

failed_unlock:
    unlock_sfs_fs(sfs);
    return ret;
}

/**
 * sfs_gettype - 返回文件的类型, sfs文件系统目前只支持两种文件类型: 常规文件和目录文件
 */
static int sfs_gettype(struct inode *node, uint32_t *type_store) 
{
    struct sfs_disk_inode *din = vop_info(node, sfs_inode)->din;
    switch (din->type) {
    case SFS_TYPE_DIR:
        *type_store = S_IFDIR;
        return 0;
    case SFS_TYPE_FILE:
        *type_store = S_IFREG;
        return 0;
    // case SFS_TYPE_LINK:
    //     *type_store = S_IFLNK;
    //     return 0;
    }
    panic("invalid file type %d.\n", din->type);
}

/** 
 * sfs_tryseek - 检查文件中的一个特定位置是否合法
 */
static int sfs_tryseek(struct inode *node, off_t pos) 
{
    if (pos < 0 || pos >= SFS_MAX_FILE_SIZE) {
        return -E_INVAL;
    }
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    if (pos > sin->din->size) {
        return vop_truncate(node, pos);
    }
    return 0;
}

/**
 * sfs_truncfile - 重新调整文件大小
 */
static int sfs_truncfile(struct inode *node, off_t len) 
{
    if (len < 0 || len > SFS_MAX_FILE_SIZE) {
        return -E_INVAL;
    }
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    struct sfs_disk_inode *din = sin->din;

    int ret = 0;
	//new number of disk blocks of file
    uint32_t nblks, tblks = ROUNDUP_DIV(len, SFS_BLKSIZE);
    if (din->size == len) {
        assert(tblks == din->blocks);
        return 0;
    }

    lock_sin(sin);
	// inode的旧磁盘块数
    nblks = din->blocks;
    if (nblks < tblks) {
		// 尝试在文件末尾增加磁盘块来增加文件大小
        while (nblks != tblks) {
            if ((ret = sfs_bmap_load_nolock(sfs, sin, nblks, NULL)) != 0) {
                goto out_unlock;
            }
            nblks ++;
        }
    }
    else if (tblks < nblks) {
		// 尝试减少文件大小
        while (tblks != nblks) {
            if ((ret = sfs_bmap_truncate_nolock(sfs, sin)) != 0) {
                goto out_unlock;
            }
            nblks --;
        }
    }
    assert(din->blocks == tblks);
    din->size = len;
    sin->dirty = 1;

out_unlock:
    unlock_sin(sin);
    return ret;
}

/**
 * sfs_lookup_subpath - 得到当前路径中的第一个分量
 */
static char *sfs_lookup_subpath(char *path)
{
    if ((path = strchr(path, '/')) != NULL) {
        if (*path == '/') {
            *path++ = '\0';
        }
        if (*path == '\0') {
            return NULL;
        }
    }
    return path;
}

/**
 * sfs_lookup - 查找相对于目录inode的路径为path的文件, 存储在node_store中返回
 * @node: 通用inode, 这里是目录文件
 * @path: 相对于传入的目录的相对路径
 * @node_store: 返回找到的文件inode
 */
static int sfs_lookup(struct inode *node, char *path, struct inode **node_store) {
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    assert(*path != '\0' && *path != '/');
    vop_ref_inc(node);
    do {
        struct sfs_inode *sin = vop_info(node, sfs_inode);
        if (sin->din->type != SFS_TYPE_DIR) {
            vop_ref_dec(node);
            return -E_NOTDIR;
        }
        // 找到当前路径的第一个分量
        char *subpath = sfs_lookup_subpath(path);
        // 在当前目录下搜索
        struct inode *subnode;
        int ret = sfs_lookup_once(sfs, sin, path, &subnode, NULL);

        vop_ref_dec(node);
        if (ret != 0) {
            return ret;
        }
        node = subnode, path = subpath;
    } while (path != NULL);
    *node_store = node;
    return 0;
}

/**
 * sfs_lookup_parent - 查找相对于目录inode路径为path的文件的父目录, 父目录文件inode存储在node_store中返回, 最后一个分量(即文件名)指针存储在endp中返回
 * @node: 通用inode, 这里是目录文件
 * @node: 通用inode, 这里是目录文件
 * @path: 相对于传入的目录的相对路径
 * @node_store: 返回找到的父目录文件inode
 * @endp: 返回文件名的指针
 */
static int sfs_lookup_parent(struct inode *node, char *path, struct inode **node_store, char **endp) 
{
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    assert(*path != '\0' && *path != '/');
    vop_ref_inc(node);
    while (1) {
        struct sfs_inode *sin = vop_info(node, sfs_inode);
        if (sin->din->type != SFS_TYPE_DIR) {
            vop_ref_dec(node);
            return -E_NOTDIR;
        }

        char *subpath = sfs_lookup_subpath(path);
        if (subpath == NULL) {
            *node_store = node, *endp = path;
            return 0;
        }

        struct inode *subnode;
        int ret = sfs_lookup_once(sfs, sin, path, &subnode, NULL);

        vop_ref_dec(node);
        if (ret != 0) {
            return ret;
        }
        node = subnode, path = subpath;
    }
}

/**
 * sfs_create_sub_nolock - 类似于sfs_load_inode, 创建文件inode
 * @sfs: sfs文件系统
 * @node_store: 返回创建的inode
 * @ino: 创建的inode所在的磁盘块号
 * @type: 创建的文件类型, 可以是常规文件或目录文件
 */
static int sfs_create_sub_nolock(struct sfs_fs *sfs, struct inode **node_store, uint32_t ino, uint32_t type)
{
    int ret = -E_NO_MEM;
    struct sfs_disk_inode *din;
    if ((din = kmalloc(sizeof(struct sfs_disk_inode))) == NULL)
        return -E_NO_MEM;

    // 从磁盘读取磁盘inode(实际上该磁盘块应为空)
    if ((ret = sfs_rbuf(sfs, din, sizeof(struct sfs_disk_inode), ino, 0)) != 0) {
        goto failed_cleanup;
    }
    // 完成磁盘inode各项初始化
    din->blocks = 0;
    din->size = 0;
    din->type = type;
    din->nlinks = 0;
    // 创建vfs层通用inode
    if ((ret = sfs_create_inode(sfs, din, ino, node_store)) != 0) {
        goto failed_cleanup;
    }

    struct sfs_inode *sin = vop_info(*node_store, sfs_inode);
    sin->dirty = 1;
    sfs_set_links(sfs, sin);

    return 0;

failed_cleanup:
    kfree(din);
    return ret;
}

/**
 * sfs_create - 创建常规文件
 * @node: vfs层通用inode, 这里是父目录
 * @name: 文件名
 * @excl: excluded, 当要创建的文件已经存在时, 如果excl为1, 就返回错误; 如果excl为0, 就直接给出已有的文件inode
 * @node_store: 返回的文件inode
 */
int sfs_create(struct inode *node, const char *name, bool excl, struct inode **node_store)
{
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int ret;
    uint32_t ino;
    /* 如果inode已存在, 
     * excl为1时, 返回错误
     * excl为0时, 给出文件的inode */
    int empty_slot;
    lock_sin(sin);
    if ((ret = sfs_dirent_search_nolock(sfs, sin, name, &ino, NULL, &empty_slot)) != -E_NOENT) {
        if (excl) {
            ret = -E_EXISTS;
            goto failed_unlock;
        }
        if ((ret = sfs_load_inode(sfs, node_store, ino)) != 0) {
            goto failed_unlock;
        }
        goto out;
    }

    /* 如果inode不存在, 创建文件的inode */
    struct inode *new_node;
    // 申请磁盘块
    if ((ret = sfs_block_alloc(sfs, &ino)) != 0) {
        goto failed_unlock;
    }
    assert(sfs_block_inuse(sfs, ino));
    // 创建inode
    if ((ret = sfs_create_sub_nolock(sfs, &new_node, ino, SFS_TYPE_FILE)) !=0) {
        goto failed_unlock;
    }
    // 建立目录项和inode之间的链接
    struct sfs_inode *new_sin = vop_info(new_node, sfs_inode);
    if ((ret = sfs_dirent_link_nolock(sfs, sin, empty_slot, new_sin, name)) != 0) {
        goto failed_unlock_cleanup;
    }
    *node_store = new_node;
out:
    unlock_sin(sin);
    return 0;

failed_unlock_cleanup:
    // 清理建立的inode
    sfs_block_free(sfs, ino);
failed_unlock:
    unlock_sin(sin);
failed:
    return ret;
}

/**
 * sfs_mkdir - 创建目录文件
 * @node: 父目录通用inode
 * @name: 目录名
 */
int sfs_mkdir(struct inode *node, const char *name)
{
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int empty_slot;
    uint32_t ino;
    int ret;
    lock_sin(sin);
    // 查找inode是否存在
    if ((ret = sfs_dirent_search_nolock(sfs, sin, name, &ino, NULL, &empty_slot)) != -E_NOENT) {
        return -E_EXISTS;
    }
    // 申请磁盘块, 用于inode
    if ((ret = sfs_block_alloc(sfs, &ino)) != 0) {
        goto failed_unlock;
    }
    // 构造目录文件inode
    struct inode *new_node;
    if ((ret = sfs_create_sub_nolock(sfs, &new_node, ino, SFS_TYPE_DIR)) != 0) {
        goto failed_unlock_cleanup;
    }
    // 链接自身到父目录的目录项上
    struct sfs_inode *new_sin = vop_info(new_node, sfs_inode);
    if ((ret = sfs_dirent_link_nolock(sfs, sin, empty_slot, new_sin, name)) != 0) {
        // assert(new_sin->din->nlinks == 0);
		// assert(inode_ref_count(new_node) == 1
		//        && inode_open_count(new_node) == 0);
        goto failed_unlock_cleanup;
    }
    // 链接自身到".", 链接父目录到".."
    sfs_dirent_link_nolock(sfs, new_sin, 0, new_sin, ".");
    sfs_dirent_link_nolock(sfs, new_sin, 1, sin, "..");

    unlock_sin(sin);
    return 0;

failed_unlock_cleanup:
    sfs_block_free(sfs, ino);
failed_unlock:
    unlock_sin(sin);
    return ret;
} 


// 目录文件对应的抽象文件接口函数
static const struct inode_ops sfs_node_dirops = {
    .vop_magic                      = VOP_MAGIC,
    .vop_open                       = sfs_opendir,
    .vop_close                      = sfs_close,
    .vop_fstat                      = sfs_fstat,
    .vop_fsync                      = sfs_fsync,
    .vop_namefile                   = sfs_namefile,
    .vop_mkdir                      = sfs_mkdir,
    .vop_link                       = sfs_link,
    .vop_getdirentry                = sfs_getdirentry,
    .vop_reclaim                    = sfs_reclaim,
    .vop_gettype                    = sfs_gettype,
    .vop_lookup                     = sfs_lookup,
    .vop_lookup_parent              = sfs_lookup_parent,
    .vop_create                     = sfs_create,
    .vop_unlink                     = sfs_unlink,
};
// 常规文件对应的抽象文件接口函数
static const struct inode_ops sfs_node_fileops = {
    .vop_magic                      = VOP_MAGIC,
    .vop_open                       = sfs_openfile,
    .vop_close                      = sfs_close,
    .vop_read                       = sfs_read,
    .vop_write                      = sfs_write,
    .vop_fstat                      = sfs_fstat,
    .vop_fsync                      = sfs_fsync,
    .vop_reclaim                    = sfs_reclaim,
    .vop_gettype                    = sfs_gettype,
    .vop_tryseek                    = sfs_tryseek,
    .vop_truncate                   = sfs_truncfile,
};

