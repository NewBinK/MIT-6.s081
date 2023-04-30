// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
extern uint ticks;
#define NBUC 13
struct {
  struct buf buf[NBUF];
} bcache;
struct {
  struct spinlock lock;
  struct buf head;
}bbucket[NBUC];

void link(struct buf* x, struct buf* y){
  x->next = y; y->prev = x;
}
void
binit(void)
{
  struct buf *b;

  // initlock(&bcache.lock, "bcache");
  for(int i = 0; i < NBUC; ++i) {//init bucket
    initlock(&bbucket[i].lock, "bcache.bucket");
    bbucket[i].head.prev = &bbucket[i].head;
    bbucket[i].head.next = &bbucket[i].head;
  }
  struct buf* x, *y;
  int id = 1;
  for(b = bcache.buf; b < bcache.buf+NBUF; ++b){//一开始均摊的分配到各个bucket
    b->blockno = id; b->dev = 0xffffffff;//initialize
    x = &bbucket[id%NBUC].head;
    y = bbucket[id%NBUC].head.next;
    link(x, b); link(b, y);
    initsleeplock(&b->lock, "buffer");
    if(id > 0) id = (id+1)%NBUC;
  }
}
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b = 0, *lru = 0;
  int id = blockno%NBUC;
  acquire(&bbucket[id].lock);
  //查看是否有可用的缓存
  for(b = bbucket[id].head.next; b != &bbucket[id].head; b = b->next){
    if(b->dev == dev && b -> blockno == blockno){//有缓存
      b->refcnt++;//ref++以免被挤掉
      release(&bbucket[id].lock);//先释放桶的锁
      acquiresleep(&b->lock);//尝试竞争这个buf
      b->tick = ticks;//更新buf的使用时间
      return b;
    }else if(b->refcnt == 0){//顺便找lru
      if(lru == 0 || lru->tick > b->tick) lru = b;
    }
  }
  //没命中，看看lru能不能用
  if(lru){//能用
    lru->dev = dev;
    lru->blockno = blockno;
    lru->valid = 0;
    lru->refcnt = 1;//初始化
    release(&bbucket[id].lock);//先释放桶的锁
    acquiresleep(&lru->lock);//尝试竞争这个buf
    b->tick = ticks;//更新buf的使用时间
    return lru;
  }
  //去其他地方探索一下吧, 此时id一定不会被其他桶视为探索对象(因为没有ref为0的buf在链上)
  while(1){
    for(b = bcache.buf; b < bcache.buf+NBUF; ++b){//这里不能锁住再检查refcnt，会产生死锁。先检查refcnt再锁，就不会锁到已经锁住并且正在检查其他bucket的桶
      if(b->refcnt == 0 && (lru == 0 || lru->tick > b->tick)) {//找一个可用的lru
        lru = b;
      }
    }
    int nid = lru->blockno%NBUC;
    acquire(&bbucket[nid].lock);//当bucket[nid]被锁住并且在获取 bucket[id] 的时候，就死锁了,所以要避免这种情况
    //并且此时要保持锁住bbucket[id],避免其他进程也尝试为dev,block寻找一个buf，导致一个(dev,blockno)被映射到多个buf上
    if(lru != 0 && lru->refcnt == 0 && lru->blockno%NBUC == nid){//确实还是可用的lru,并且依然是在bucket[nid]中
      //初始化
      lru->dev = dev;
      lru->blockno = blockno;
      lru->valid = 0;
      lru->refcnt = 1;
      struct buf *x, *y;
      //把lru从bucket[nid]中删除
      x = lru->prev, y = lru->next;
      link(x, y);
      release(&bbucket[nid].lock);//此时不再需要和bucket[nid]中的任何内容交互,可释放锁
      //把lru加入bucket[id]
      x = &bbucket[id].head;
      y = bbucket[id].head.next;
      link(x, lru); link(lru, y);
      release(&bbucket[id].lock);//释放当前桶的锁
      acquiresleep(&lru->lock);//由于这里已经释放bucket的锁，所以有可能有其他进程调用gets语句率先获取到了lru
      lru->tick = ticks;
      return lru;
    }else{//来晚了, lru对应的buf被别人拿走了
      release(&bbucket[nid].lock);//释放nid的锁，找其他的buf
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);//允许其他进程获取这个buf
  int id = b->blockno%NBUC;
  acquire(&bbucket[id].lock);
  --b->refcnt;
  release(&bbucket[id].lock);
}

void
bpin(struct buf *b) {
  int id = b->blockno%NBUC;
  acquire(&bbucket[id].lock);
  b->refcnt++;
  release(&bbucket[id].lock);
}

void
bunpin(struct buf *b) {
  int id = b->blockno%NBUC;
  acquire(&bbucket[id].lock);
  b->refcnt--;
  release(&bbucket[id].lock);
}


