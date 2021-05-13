#ifndef __KERN_SCHEDULE_CFS_RBTREE_H__
#define __KERN_SCHEDULE_CFS_RBTREE_H__

#include<proc.h>

struct cfs_node
{
  struct rb_node node;
  struct proc_struct *proc;
};

struct cfs_node *cfs_search(struct rb_root *root, struct proc_struct *proc);
int cfs_insert(struct rb_root *root, struct proc_struct *proc);
void cfs_node_free(struct cfs_node *node);
struct proc_struct * cfs_find_min(struct rb_root *root);
int compare_cfs_node(struct proc_struct *a, struct proc_struct *b);


#endif