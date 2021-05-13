#ifndef __KERN_SCHEDULE_stride_RBTREE_H__
#define __KERN_SCHEDULE_stride_RBTREE_H__

#include<proc.h>

struct stride_node
{
  struct rb_node node;
  struct proc_struct *proc;
};

struct stride_node *stride_search(struct rb_root *root, struct proc_struct *proc);
int stride_insert(struct rb_root *root, struct proc_struct *proc);
void stride_node_free(struct stride_node *node);
struct proc_struct * stride_find_min(struct rb_root *root);
int compare_stride_node(struct proc_struct *a, struct proc_struct *b);


#endif