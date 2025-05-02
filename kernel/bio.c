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
    b->timestamp = ticks;
    initsleeplock(&b->lock, "buffer");
    // Note that this is for initialization. In bget (or evict()), we will
    // update the correct block number and push it into the correct bucket.
    push(b, 0);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
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

  //printf("b: %d\n", b->refcnt);

  /*
  int max_attempts = 5;
  while (b == NULL && max_attempts > 0) {
    yield();
    b = evict();
    max_attempts--;
  }
  */

  b = evict();
  if (b != NULL) {
    acquire(&bcache.htable[id].bucket_lock);

    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;

    push(b, id);

    release(&bcache.htable[id].bucket_lock);
    acquiresleep(&b->lock);
    return b;
  }

  panic("bget: no buffers");
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
          lowest_timestamp = b->timestamp;
          candidate = b;
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

  uint id = hash(candidate->blockno);
  acquire(&bcache.htable[id].bucket_lock);
  // Instead, directly traverse the list and verify the candidate still exists
  // to avoid a double free.
  struct buf *curr = NULL;

  for (curr = bcache.htable[id].head.next; curr != &bcache.htable[id].head; curr = curr->next) {
    if (curr == candidate) {
      if (candidate->refcnt > 0) {
        release(&bcache.htable[id].bucket_lock);
        goto retry;
      }
      candidate->next->prev = candidate->prev;
      candidate->prev->next = candidate->next;

      release(&bcache.htable[id].bucket_lock);
      release(&bcache.lock);
      return candidate;
    }
  }
  release(&bcache.htable[id].bucket_lock);
  release(&bcache.lock);
  return NULL;
  // Three Scenarios: 
  // 1) It passes, in which it makes out of the if statement below and doesn't double free.
  // 2) It gets stuck in the if statement, and infinitely loops.
  // 3) It double frees.
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
  if (b->refcnt == 0) {
    b->timestamp = ticks;
  }
  release(&bcache.htable[id].bucket_lock);
}

void
bpin(struct buf *b) {
  uint id = hash(b->blockno);
  acquire(&bcache.htable[id].bucket_lock);
  b->refcnt++;
  release(&bcache.htable[id].bucket_lock);
}

void
bunpin(struct buf *b) {
  uint id = hash(b->blockno);
  acquire(&bcache.htable[id].bucket_lock);
  b->refcnt--;
  release(&bcache.htable[id].bucket_lock);
}


  /*

  // Evict the candidate buffer
  //uint id = hash(candidate->blockno);
  acquire(&bcache.htable[candidate_bucket].bucket_lock);
  //printf("Candidate bucket and id: %d, %d\n", candidate_bucket, id);
  // someone changed refcnt 
  if (candidate->refcnt > 0) {
    printf("CANDIDATE REFCNT: %d\n", candidate->refcnt);
    release(&bcache.htable[candidate_bucket].bucket_lock);
    //goto retry;
    return NULL;
  }

  // NOTE: Pin the candidate during the return so another 
  // process can't modify it during that time.
  //candidate->refcnt = 1;
  //printf("CANDIDATE REFCNT AFTER RETRY: %d\n", candidate->refcnt);
  //candidate->refcnt = 1;

  // remove the candidate
  //pop(candidate);

  // release all locks in acquisition order
  release(&bcache.htable[candidate_bucket].bucket_lock);
  release(&bcache.lock);
  // and finally, return relevant buffer
  return candidate;
  */
