#include <defs.h>
#include <list.h>
#include <proc.h>
#include <assert.h>
#include <stdio.h>
#include <default_sched.h>
#include <rbtree.h>
#include <cfs_rb_tree.h>

static void
cfs_init(struct run_queue *rq)
{
  list_init(&(rq->run_list));
  rq->cfs_rb_tree = RB_ROOT;
  rq->proc_num = 0;
}

static void
cfs_enqueue(struct run_queue *rq, struct proc_struct *proc)
{
  cfs_insert(&(rq->cfs_rb_tree), proc);
  if (proc->time_slice == 0 || proc->time_slice > rq->max_time_slice)
    proc->time_slice = rq->max_time_slice;
  proc->rq = rq;
  rq->proc_num++;
}

static void
cfs_dequeue(struct run_queue *rq, struct proc_struct *proc)
{
  struct cfs_node *data = cfs_search(&(rq->cfs_rb_tree), proc);
  if (data)
  {
    rb_erase(&data->node, &(rq->cfs_rb_tree));
    cfs_node_free(data);
    rq->proc_num--;
  }
}

static struct proc_struct *
cfs_pick_next(struct run_queue *rq)
{
  // 选不出来 p 可能为 NULL
  struct proc_struct *p = (struct proc_struct *)cfs_find_min(&(rq->cfs_rb_tree));
  return p;
}

static void
cfs_proc_tick(struct run_queue *rq, struct proc_struct *proc)
{
  // 实际上CFS的实现：若 proc->vruntime 已经不是队列里面最小的了（实际上会导致频繁调度，所以一般设置一个阈值）
  // 超过这个限制会直接触发调度

  // 此时的进程不在红黑树中，修改 proc->vruntime 不会破坏红黑树的性质。 但是还是需要完善上述 CFS

  // 仍有运行时间则减少其运行时间，计算虚拟运行时间
  assert(proc->cfs_prior != 0);
  proc->vruntime += proc->cfs_prior;
  if (proc->time_slice > 0)
    proc->time_slice--;
  // 时间片用完则标记该进程需要调度
  if (proc->time_slice == 0)
    proc->need_resched = 1;
}

struct sched_class cfs_sched_class = {
    .name = "cfs_scheduler",
    .init = cfs_init,
    .enqueue = cfs_enqueue,
    .dequeue = cfs_dequeue,
    .pick_next = cfs_pick_next,
    .proc_tick = cfs_proc_tick,
};
