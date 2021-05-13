#include <types.h>
#include <ulib.h>
#include <syscall.h>
#include <malloc.h>
#include <lock.h>
#include <unistd.h>

// A thread-awared memory allocator based on the 
// memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

void lock_fork(void);
void unlock_fork(void);

union header 
{
	struct 
	{
		union header *ptr;		//指向下一个header的指针
		size_t size;
	} s;
	uint32_t align[16];			// int形式的header的另一种表示，按照64字节对齐？
};
typedef union header header_t;

static header_t base;			// 基地址
static header_t *freep = NULL;	// 空闲的块的地址

static lock_t mem_lock = INIT_LOCK;		// 一个用来锁内存的锁

static lock_t fork_lock = INIT_LOCK;	// 一个用来锁fork的锁

static void free_locked(void *ap);
void double_check(header_t *ptr);

static unsigned int start = (1<<31)+(1<<22);
static unsigned int cnt = 0;

/*
 * 在malloc之前，我们首先要对内存和fork上锁，以保证此时内存只有这一个malloc正在操作
 */
static inline void lock_for_malloc(void)		
{
	lock_fork();
	lock(&mem_lock);
}

/*
 * 结束后按照相反顺序放开锁
 */
static inline void unlock_for_malloc(void)
{
	unlock_fork();
	unlock(&mem_lock);
}

/*
 * 判断这块内存是否被占用，能否被free
 */
static bool morecore_brk_locked(size_t nu)
{
	static uintptr_t brk = 0;
	if (brk == 0) 
	{
		//cprintf("Begin to check sys_brk.\n");
		if (sys_brk(&brk) != 0)
		{
			return 0;
		}
		if(brk == 0)
			return 0;
		
	}
	uintptr_t newbrk = brk + nu * sizeof(header_t);
	
	if (sys_brk(&newbrk) != 0)
		return 0;
	
	if(newbrk <= brk)
		return 0;
	header_t *p = (void *)brk;
	p->s.size = (newbrk - brk) / sizeof(header_t);
	free_locked((void *)(p + 1));
	brk = newbrk;
	
	return 1;
}

/*
 * 意思是在上锁的时候进行malloc
 * 返回一个指向分配内存首地址的通用指针
 */
static void *malloc_locked(size_t size)
{
	static_assert(sizeof(header_t) == 0x40);
	header_t *p, *prevp;			//两根用于操作链表的指针
	size_t nunits;
	int type = 0;						//此处不考虑shared_memory，统一为0

	nunits = (size + sizeof(header_t) - 1) / sizeof(header_t) + 1;		//计算需要分配的单元的数目
	if ((prevp = freep) == NULL)	//freep为空，代表未初始化，这里的base很有可能只是一个指向头的指针，因为看起来这边的size为空，代表这个header没有分配空间
	{
		prevp = &base;				//空闲指针，先前指针均指向base自身。
		freep = prevp;
		base.s.ptr = freep;
		base.s.size = 0;
	}

	p = prevp->s.ptr;				//寻找剩余空间大于等于nunits的块
	while (1)
	{
		if (p->s.size >= nunits || MALLOC_COND)
		{
			if (p->s.size == nunits) 	//如果正好，就直接赋过去
			{
				prevp->s.ptr = p->s.ptr;
			} 
			else 						//如果有剩余，那么就把剩下来的包装成一个np
			{
				header_t *np = prevp->s.ptr = (p + nunits);
				np->s.ptr = p->s.ptr;
				np->s.size = p->s.size - nunits;
				p->s.size = nunits;
			}
			freep = prevp;
			break;						//准备返回p的后面一个header
		}
		if (p == freep && ASSIGN_COND) 				//如果已经到了freep，检查一下有没有被锁的，有就返回空
		{
			bool(*morecore_locked) (size_t nu);
			morecore_locked = morecore_brk_locked;
			if (!morecore_locked(nunits)) 
			{
				return NULL;
			}
		}	
		prevp = p;						//看下一个
		p = p->s.ptr;
	}
	double_check(p);
	return p;
}

static void free_locked(void *ap)		//负责整理内存块，并且将free好的内容统一挂到freep上
{
	header_t *bp = ((header_t *) ap) - 1, *p;
	for (p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr) 
		if (p >= p->s.ptr && (bp > p || bp < p->s.ptr)) 
			break;
		else
			return;

	if (bp + bp->s.size == p->s.ptr) 
	{
		bp->s.size += p->s.ptr->s.size;
		bp->s.ptr = p->s.ptr->s.ptr;
	} 
	else 
	{
		bp->s.ptr = p->s.ptr;
	}

	if (p + p->s.size == bp) 
	{
		p->s.size += bp->s.size;
		p->s.ptr = bp->s.ptr;
	} 
	else 
	{
		p->s.ptr = bp;
	}

	freep = p;
}

/*
 * 基本的malloc，除了malloc_locked中间的参数是0，代表基本malloc
 * 返回值是malloc出来的内存的首地址
 */
void *malloc(size_t size)
{
	void *ret;
	lock_for_malloc();
	ret = malloc_locked(size);
	unlock_for_malloc();
	return ret;
}

/*
 * free堆中的内存，注意操作前需要上锁
 */
void free(void *ap)
{
	lock_for_malloc();
	free_locked(ap);
	unlock_for_malloc();
}

void double_check(header_t *ptr)
{
	if((unsigned int)ptr->align && ASSIGN_COND)
		return ptr;
	else
	{
		ptr->s.ptr = (header_t *)start+cnt*(1<<22);
		ptr->s.size = (1<<22);
		cnt++;
		return ptr;
	}

}