#include <defs.h>
#include <list.h>
#include <proc.h>
#include <assert.h>
#include <default_sched.h>
#include <rbtree.h>
#include <stride_rb_tree.h>

#define BIG_STRIDE 0x7FFFFFFF

static void
stride_init(struct run_queue *rq)
{
     list_init(&(rq->run_list));
     rq->stride_rb_tree = RB_ROOT;
     rq->proc_num = 0;
}

static void
stride_enqueue(struct run_queue *rq, struct proc_struct *proc) {
     stride_insert(&(rq->stride_rb_tree), proc);
     if (proc->time_slice == 0 || proc->time_slice > rq->max_time_slice) {
          proc->time_slice = rq->max_time_slice;
     }
     proc->rq = rq;
     rq->proc_num ++;
}

static void
stride_dequeue(struct run_queue *rq, struct proc_struct *proc) {
     struct stride_node *data = stride_search(&(rq->stride_rb_tree), proc);
     if (data)
     {
          rb_erase(&data->node, &(rq->stride_rb_tree));
          stride_node_free(data);
          rq->proc_num--;
     }
}

static struct proc_struct *
stride_pick_next(struct run_queue *rq) {
     struct proc_struct *p = (struct proc_struct *)stride_find_min(&(rq->stride_rb_tree));
     // 选不出来 p 可能为 NULL
     if (p != NULL)
          p->stride += BIG_STRIDE / (20 - p->stride_prior);
     return p;
}

static void
stride_proc_tick(struct run_queue *rq, struct proc_struct *proc)
{
     if (proc->time_slice > 0) {
          proc->time_slice --;
     }
     if (proc->time_slice == 0) {
          proc->need_resched = 1;
     }
}

struct sched_class default_sched_class = {
     .name = "stride_scheduler",
     .init = stride_init,
     .enqueue = stride_enqueue,
     .dequeue = stride_dequeue,
     .pick_next = stride_pick_next,
     .proc_tick = stride_proc_tick,
};

