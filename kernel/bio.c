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

#define NBUCKETS 13
#define hash(dev, blockno) ((dev << 5) + blockno)%NBUCKETS
/*
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;
*/

struct bucket {
  struct sleeplock bucket_lock;
  // linked list of bufs
  struct buf* head;
};

struct {
  // protects the freelist
  struct spinlock lock;
  struct buf buf[NBUF];

  struct bucket hashtable[NBUCKETS];
  // free list for bufs with refcnt = 0;
  struct buf* freelist;

} bcache;

void
hinit(void)
{
  for(int i = 0; i < NBUCKETS; ++i) {
    initsleeplock(&bcache.hashtable[i].bucket_lock, "bcache.bucket");
    bcache.hashtable[i].head = 0;
  }
}

static struct buf*
hget(uint dev, uint blockno)
{
  uint hash_idx = hash(dev, blockno);
  acquiresleep(&bcache.hashtable[hash_idx].bucket_lock);
  // check if there is a cached buf for (dev, blockno)
  struct buf* b;
  //printf("head: %p, hashidx: %d\n", bcache.hashtable[hash_idx].head, hash_idx);
  b=bcache.hashtable[hash_idx].head;
  //printf("b: %p\n", b);
  for(b=bcache.hashtable[hash_idx].head; b != 0; b=b->next){
    //printf("b: %p, hashidx: %d\n", b, hash_idx);
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      releasesleep(&bcache.hashtable[hash_idx].bucket_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // no block, allocate from bcache freelist'
  acquire(&bcache.lock);
  if(bcache.freelist == 0) {
    panic("hget: no buffers");
  }
  //printf("allocating block: %p for dev %d, and blockno %d\n", bcache.freelist, dev, blockno);
  b = bcache.freelist;
  bcache.freelist = bcache.freelist->next;
  if(bcache.freelist != 0) bcache.freelist->prev = 0;
  b->next = bcache.hashtable[hash_idx].head;
  release(&bcache.lock);

  if(b->next != 0) {
    b->next->prev = b;
  }
  b->prev = 0;
  bcache.hashtable[hash_idx].head = b;
  b->refcnt = 1;
  b->valid = 0;
  b->dev = dev;
  b->blockno = blockno;
  //printf("bucket head: %p, hashidx %d blockno %d\n", bcache.hashtable[hash_idx].head, hash_idx, blockno);
  releasesleep(&bcache.hashtable[hash_idx].bucket_lock);
  acquiresleep(&b->lock);
  return b;
}

void
binit(void)
{

  initlock(&bcache.lock, "bcache");

  // Create doubly linked list of buffers
  bcache.freelist=&bcache.buf[0];
  bcache.freelist->prev = 0;
  if(bcache.freelist == 0) {
    panic("binit: freelist should not be null\n");
  }
  for(int i = 0; i < NBUF-1; ++i) {
    bcache.buf[i].next = &bcache.buf[i+1];
    bcache.buf[i+1].prev = &bcache.buf[i];
    bcache.buf[i].valid = 0;
    bcache.buf[i].refcnt = 1;
    initsleeplock(&bcache.buf[i].lock, "bcache.buffer");
  }
  bcache.buf[NBUF-1].next = 0;
  bcache.buf[NBUF-1].valid = 0;
  bcache.buf[NBUF-1].refcnt = 1;
  initsleeplock(&bcache.buf[NBUF-1].lock, "bcache.buffer");

  // init hashtable
  hinit();
 }

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  return hget(dev, blockno);
}
/*
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}
*/

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
  int remove = 0;
  if(!holdingsleep(&b->lock))
    panic("brelse");

  uint hash_idx = hash(b->dev, b->blockno);

  //printf("releasing: b %p, dev %d blockno %d hashidx %d\n", b,b->dev,b->blockno,hash_idx);
  b->refcnt--;
  if (b->refcnt == 0) {
    remove = 1;
  }

  if(remove == 1) {
    acquiresleep(&bcache.hashtable[hash_idx].bucket_lock);
  //printf("remove before: bucket head %p\n", bcache.hashtable[hash_idx].head);
    if(b->prev != 0) {
      b->prev->next = b->next;
    }
    if(b->next != 0) {
      b->next->prev = b->prev;
    }
    if(b== bcache.hashtable[hash_idx].head) {
      // is head
      bcache.hashtable[hash_idx].head = b->next;
    }
  //printf("remove after: bucket head %p\n", bcache.hashtable[hash_idx].head);
    releasesleep(&bcache.hashtable[hash_idx].bucket_lock);

    // add to freelist
    acquire(&bcache.lock);

    //printf("remove before: freelist head %p\n", bcache.freelist);
    b->next = bcache.freelist;
    if(bcache.freelist != 0) bcache.freelist->prev = b;
    bcache.freelist = b;
    //printf("remove after: freelist head %p\n", bcache.freelist);
    release(&bcache.lock);
  }
  releasesleep(&b->lock);
}

void
bpin(struct buf *b) {
  //acquiresleep(&b->lock);
  b->refcnt++;
  //releasesleep(&b->lock);
}

void
bunpin(struct buf *b) {
  //acquiresleep(&b->lock);
  b->refcnt--;
  uint hash_idx = hash(b->dev, b->blockno);
  if(b->refcnt == 0) {
    acquiresleep(&bcache.hashtable[hash_idx].bucket_lock);
    if(b->prev != 0) {
      b->prev->next = b->next;
    }
    if(b->next != 0) {
      b->next->prev = b->prev;
    }
    if(b== bcache.hashtable[hash_idx].head) {
      // is head
      bcache.hashtable[hash_idx].head = b->next;
    }
    releasesleep(&bcache.hashtable[hash_idx].bucket_lock);

    // add to freelist
    acquire(&bcache.lock);
    b->next = bcache.freelist;
    if(bcache.freelist != 0) bcache.freelist->prev = b;
    release(&bcache.lock);
  }

}

