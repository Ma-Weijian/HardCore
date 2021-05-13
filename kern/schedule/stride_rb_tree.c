#include <rbtree.h>
#include <stride_rb_tree.h>
#include <kmalloc.h>

int compare_stride_node(struct proc_struct *a, struct proc_struct *b)
{
  int32_t c = a->stride - b->stride;
  if (c != 0)
  {
    if (c > 0)
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

struct stride_node *stride_search(struct rb_root *root, struct proc_struct *proc)
{
  struct rb_node *node = root->rb_node;

  while (node)
  {
    struct stride_node *data = container_of(node, struct stride_node, node);
    if (data->proc->pid == proc->pid)
      return data;
    if (compare_stride_node(data->proc, proc))
      node = node->rb_left;
    else
      node = node->rb_right;
  }
  return NULL;
}

int stride_insert(struct rb_root *root, struct proc_struct *proc)
{
  struct stride_node *data = (struct stride_node *)kmalloc(sizeof(struct stride_node));
  data->proc = proc;
  struct rb_node **new = &(root->rb_node), *parent = NULL;

  /* Figure out where to put new node */
  while (*new)
  {
    struct stride_node *this = container_of(*new, struct stride_node, node);

    int result = compare_stride_node(data->proc, this->proc);
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

struct proc_struct *stride_find_min(struct rb_root *root)
{
  struct rb_node *node = root->rb_node;
  if (node == NULL)
    return NULL;
  while (node->rb_left)
    node = node->rb_left;
  struct stride_node *data = container_of(node, struct stride_node, node);
  return data->proc;
}

void stride_node_free(struct stride_node *node)
{
  if (node != NULL)
  {
    node->proc = NULL;
    kfree(node);
    node = NULL;
  }
}
