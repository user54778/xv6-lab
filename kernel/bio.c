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

#define BUCKETS 13

// Instead of a single global bcache lock on all I/O operations, we attempt to reduce 
// contention.... how?
// Essentially, each cached block has its own lock; this is not enough -> Can you think of why?
// Hence, we have a global lock with a set of cached blocks; One lock per *set* of items, and another *per item*.
// How do we remove this global lock, while maintaining this invariant of lock per set, and lock per item?
// Have an array of N locks, where we *hash* into bucket h = blocknum mod N
// Let us state N = 13 
// Then, acquire(locks[h]).
// Finally, walk the DLL of buffers in *that* bucket's list to find the cached block.
// This will require us to tweak our original implementation!
// Instead of a global list of all buffers (i.e., bcache.head....), we will timestamp buffers
// in each individual bucket using ticks from kernel/trap.c.
// Will we still need our global bcache.lock? -> YES. In the case of eviction. Why?
// In eviction, we need to modify a *GLOBAL* order of ALL 

// A unit of a hash table of buffer caches.
struct bucket {
  struct spinlock bucket_lock;
  struct buf head;
};

struct {
  // This lock will only be used for serialization of eviction scanning
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
  // Instead, this:
  struct bucket htable[BUCKETS];
} bcache;

void bprint(void); 
struct buf* evict(void);

uint
hash(uint blockno) {
  return blockno % BUCKETS;
}

// push buf to blockno in htable
void
push(struct buf* buf, uint blockno) {
  buf->next = bcache.htable[blockno].head.next;
  buf->prev = &bcache.htable[blockno].head;
  bcache.htable[blockno].head.next->prev = buf;
  bcache.htable[blockno].head.next = buf;
}

// pop buf
void
pop(struct buf *buf) {
  buf->next->prev = buf->prev;
  buf->prev->next = buf->next;
}

void
binit(void)
{
  //printf("binit\n");
  struct buf *b;

  // only for use in eviction
  initlock(&bcache.lock, "bcache_global");

  // init buckets
  for (int i = 0; i < BUCKETS; i++) {
    initlock(&bcache.htable[i].bucket_lock, "bcache");
    bcache.htable[i].head.prev = &bcache.htable[i].head;
    bcache.htable[i].head.next = &bcache.htable[i].head;
  }

  // We still want to make a LL of buffers, but on a bucket-by-bucket basis instead.
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->refcnt = 0;
    b->timestamp = 0;
    initsleeplock(&b->lock, "buffer");
    // Note that this is for initialization. In bget (or evict()), we will
    // update the correct block number and push it into the correct bucket.
    push(b, 0);
  }

  /*
  for (int i = 0; i < BUCKETS; i++) {
    printf("%d HEAD: ", i);
    for (b = bcache.htable[i].head.next; b != &bcache.htable[i].head; b = b->next) {
      printf(" blockno: %d, dev: %d->", b->blockno, b->dev);
    }
    printf("%d HEAD: ", i);
  }
  */

  /*
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    // bcache.head.prev would only update the bcache struct, 
    // NOT the actual current first buf.
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  */
  //bprint();
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  //printf("bget");
  struct buf *b;

  uint id = hash(blockno);
  acquire(&bcache.htable[id].bucket_lock);

  // is the block in the bucket already cached?
  for (b = bcache.htable[id].head.next; b != &bcache.htable[id].head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      // pin
      b->refcnt++;
      release(&bcache.htable[id].bucket_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Release bucket lock for evict()
  release(&bcache.htable[id].bucket_lock);

  b = evict();
  if (b != NULL) {
    //printf("in not null\n");
    acquire(&bcache.htable[id].bucket_lock);
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    b->timestamp = ticks; // ??
    push(b, id);
    release(&bcache.htable[id].bucket_lock);

    acquiresleep(&b->lock);
    return b;
  }

  panic("bget: no buffers");
  /*
  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      //bprint();
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
  */
}

// Evict a buffer from a bucket with the lowest timestamp.
// Return NULL if every cache is full.
// Otherwise, returns the buffer with space.
struct buf* 
evict() {
  // When replacing a block, you might move a struct buf from one bucket to another bucket, 
  // because the new block hashes to a different bucket. You might have a tricky case: 
  // the new block might hash to the same bucket as the old block. 
  // Make sure you avoid deadlock in that case. 
  //
  // This should recycle the LRU buffer based on timestamp instead of position.
  //printf("evict\n");
  struct buf *b;
  uint lowest_timestamp = 0xFFFFFFFF;
  struct buf *candidate = NULL; 

  acquire(&bcache.lock);
retry:
  // Attempt to find LRU buffer based on timestamp + refcnt
  for (int i = 0; i < BUCKETS; i++) {
    acquire(&bcache.htable[i].bucket_lock);
    for (b = bcache.htable[i].head.next; b != &bcache.htable[i].head; b = b->next) {
      // candidate for eviction
      if (b->refcnt == 0) {
        // only mark as actual eviction candidate if < seen lowest_timestamp
        if (b->timestamp < lowest_timestamp) {
          candidate = b;
          lowest_timestamp = b->timestamp;
        }
      } 
    }
    release(&bcache.htable[i].bucket_lock);
  }

  // no suitable candidates for eviction
  if (candidate == NULL) {
    release(&bcache.lock);
    return NULL;
  }

  // Evict the candidate buffer
  uint id = hash(candidate->blockno);
  acquire(&bcache.htable[id].bucket_lock);
  // someone changed refcnt 
  if (candidate->refcnt > 0) {
    release(&bcache.htable[id].bucket_lock);
    goto retry;
  }

  //printf("removing candidate\n");
  //printf("%d\n", candidate->blockno);
  // remove the candidate
  pop(candidate);
  // release all locks in acquisition order
  release(&bcache.htable[id].bucket_lock);
  release(&bcache.lock);
  // and finally, return relevant buffer
  return candidate;
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

  releasesleep(&b->lock);

  uint id = hash(b->blockno);
  acquire(&bcache.htable[id].bucket_lock);
  b->refcnt--;
  // no longer move to head of MRU; instead simply note timestamp for use 
  // in eviction if no one waiting
  b->timestamp = ticks;
  release(&bcache.htable[id].bucket_lock);
  /*
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // pop head
    b->next->prev = b->prev;
    b->prev->next = b->next;
    // push head
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  release(&bcache.lock);
  */
}

/*
void
bprint(void) {
  struct buf *b = NULL;
  int count = 0;

  acquire(&bcache.lock);

  printf("head-> ");

  for (b = bcache.head.next; b != &bcache.head; b = b->next) {
    printf(" blockno: %d, dev: %d->", b->blockno, b->dev);
    if (++count > NBUF) {
      printf("bprint: infinite loop\n");
      break;
    }
  }
  printf("head\n");
  release(&bcache.lock);
}
*/

void
bpin(struct buf *b) {
  /*
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
  */

  uint id = hash(b->blockno);
  acquire(&bcache.htable[id].bucket_lock);
  b->refcnt++;
  release(&bcache.htable[id].bucket_lock);
}

void
bunpin(struct buf *b) {
  /*
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
  */
  uint id = hash(b->blockno);
  acquire(&bcache.htable[id].bucket_lock);
  b->refcnt--;
  release(&bcache.htable[id].bucket_lock);
}
