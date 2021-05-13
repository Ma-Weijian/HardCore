#include <rbtree.h>
#include <cfs_rb_tree.h>
#include <kmalloc.h>

// 在 linux 中的红黑树中，若两个 value 大小相等会放弃插入，但是连续插入的两个 vruntime 很可能相同，针对这种情况，重新规定红黑树的排序规则
// vruntime 不同时比较 vruntime ，vruntime大者大
// vruntime 相同时比较 pid ，pid大者大.   因为树里面不可能有两个相同的进程,所以该排序可以保证线序关系

int compare_cfs_node(struct proc_struct *a, struct proc_struct *b)
{
  if (a->vruntime != b->vruntime)
  {
    if (a->vruntime > b->vruntime)
      return 1;
    else
      return -1;
  }
  else
  {
    if (a->pid > b->pid)
      return 1;
    else
      return -1;
  }
  return 0;
}

struct cfs_node *cfs_search(struct rb_root *root, struct proc_struct *proc)
{
  struct rb_node *node = root->rb_node;

  while (node)
  {
    struct cfs_node *data = container_of(node, struct cfs_node, node);
    if (data->proc->pid == proc->pid)
      return data;
    if (compare_cfs_node(data->proc, proc))
      node = node->rb_left;
    else
      node = node->rb_right;
  }
  return NULL;
}

int cfs_insert(struct rb_root *root, struct proc_struct *proc)
{
  struct cfs_node *data = (struct cfs_node *)kmalloc(sizeof(struct cfs_node));
  data->proc = proc;
  struct rb_node **new = &(root->rb_node), *parent = NULL;

  /* Figure out where to put new node */
  while (*new)
  {
    struct cfs_node *this = container_of(*new, struct cfs_node, node);

    int result = compare_cfs_node(data->proc, this->proc);
    parent = *new;
    if (result < 0)
      new = &((*new)->rb_left);
    else if (result > 0)
      new = &((*new)->rb_right);
    else
      return 0;
  }

  /* Add new node and rebalance tree. */
  rb_link_node(&data->node, parent, new);
  rb_insert_color(&data->node, root);
  return 1;
}

struct proc_struct *cfs_find_min(struct rb_root *root)
{
  struct rb_node *node = root->rb_node;
  if (node == NULL)
    return NULL;
  while (node->rb_left)
    node = node->rb_left;
  struct cfs_node *data = container_of(node, struct cfs_node, node);
  return data->proc;
}

void cfs_node_free(struct cfs_node *node)
{
  if (node != NULL)
  {
    node->proc = NULL;
    kfree(node);
    node = NULL;
  }
}
