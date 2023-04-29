// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

int get_cpuid(){
  push_off();
  int id = cpuid();
  pop_off();
  return id;
}
void
kinit()
{
  initlock(&kmem[get_cpuid()].lock, "kmem");
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  int id = get_cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int id = get_cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r){//can be alloced
    kmem[id].freelist = r->next;
    release(&kmem[id].lock);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }else{
    release(&kmem[id].lock);//先放弃当前的lock避免死锁

    for(int i = 0; i < NCPU; ++i){//去其他CPU找
      if(i == id) continue;
      acquire(&kmem[i].lock);
      
      if(kmem[i].freelist){//找到了
        r = kmem[i].freelist;
        kmem[i].freelist = r->next;//偷过来
        release(&kmem[i].lock);
        memset((char*)r, 5, PGSIZE);
        break;
      }
      release(&kmem[i].lock);
    }
  }
  return (void*)r;
}
