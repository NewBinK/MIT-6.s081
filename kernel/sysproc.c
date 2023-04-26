#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc);
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 addr; argaddr(0, &addr);
  int len; argint(1, &len);
  uint64 mask; argaddr(2, &mask);
  // unsigned int buf[32];
  // memset(buf, 0, sizeof buf);
  struct proc *cur_proc = myproc();
  if(len > 32*32){
    panic("too many page to check");
  }

  uint64 pte_mask = PGROUNDDOWN(mask);
  uint64 pa_mask = walkaddr(cur_proc->pagetable, pte_mask) + mask - pte_mask; //mask的物理地址
  unsigned* buf = (unsigned int*) pa_mask;

  for(int i = 0; i < len; ++i){
    pte_t *pte;
    if((pte = walk(cur_proc->pagetable, addr + i*PGSIZE, 0)) != 0){
      if(*pte & PTE_A) {
        // printf("???");
        *pte &= (~PTE_A);
        buf[i/32] |= 1<<(i&31);
      }else{
        buf[i/32] &= ~(1LL<<(i&31));
      }
    }
  }
  return 0;
}
//copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
