// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

int ref_vm[1<<15];
uint64 pa_index(const void*pa){
  if((uint64)pa >= PHYSTOP || (uint64)pa < KERNBASE){
    printf("get ref out of [%p,%p): %p\n", KERNBASE, PHYSTOP, pa);
  }
  uint64 index = ((uint64)(pa) - KERNBASE)>>12;
  return index;
}
int get_ref(const void*pa){//get ref
  uint64 index = pa_index(pa);
  if(index >= (1<<15)){
    printf("get ref out of %p: %p\n", PHYSTOP, pa);
    return -1;
  }
  return ref_vm[index];
}
void update_ref(const void*pa, int x){//原子性更新ref
  uint64 index = pa_index(pa);
  if(index >= (1<<15)){
    printf("get ref out of %p: %p\n", PHYSTOP, pa);
    panic("update ref fail");
  }
  acquire(&kmem.lock);
  ref_vm[index] += x;
  release(&kmem.lock);
}
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  acquire(&kmem.lock);
  if(get_ref(pa) > 0) ref_vm[pa_index(pa)] --;//减引用, freerange的时候是例外,因此只在>0的时候减少
  
  if(get_ref(pa) >= 1){//还有引用，直接减少引用
    
    release(&kmem.lock); //记得释放锁
    return;
  }
  release(&kmem.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  if(r){
    ref_vm[pa_index(r)]++; //初始分配的时候增加引用
  }
  return (void*)r;
}
