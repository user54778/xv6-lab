// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include <sys/types.h>

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

// refcnt is a per-page data structure
// that tracks how many references a page has.
// It is protected by a spinlock.
struct refcnt {
  struct spinlock lock;
  int count;
};

// A global array of refcnts that is indexed by the PFN.
// We index this array with the page's physical address / pgsize
// Our global array is simply the size of all physical memory that could be allocated.
struct refcnt global_refcnt[PHYSTOP / PGSIZE];

// Initialize the global_refcnt array
void ref_init() {
  for (int i = 0; i < PHYSTOP / PGSIZE; i++) {
    global_refcnt[i].count = 0;
    initlock(&global_refcnt[i].lock, "cow_refcnt"); 
  }
}

// Increment the refcnt
void ref_incr(uint64 pa) {
  int index = PA2IDX(pa);
  acquire(&global_refcnt[index].lock);
  global_refcnt[index].count++;
  //printf("Incrementing %d with new count %d\n", index, global_refcnt[index].count);
  release(&global_refcnt[index].lock);
}

// Decrement the refcnt
int ref_decr(uint64 pa) {
  int index = PA2IDX(pa);
  int ret;
  acquire(&global_refcnt[index].lock);
  if ((global_refcnt[index].count) < 0) {
    panic("ref_decr: negative refcnt");
  } 
  // Don't decrement non-relevant pages
  if ((global_refcnt[index].count) == 0) {
    release(&global_refcnt[index].lock);
    return 0;
  }
  global_refcnt[index].count--;
  //printf("Decrementing %d with new count %d\n", index, global_refcnt[index].count);
  ret = global_refcnt[index].count;
  release(&global_refcnt[index].lock);

  return ret;
}

// not needed?
void ref_reset(uint64 pa) {
  int index = PA2IDX(pa);
  acquire(&global_refcnt[index].lock);
  global_refcnt[index].count = 0;
  release(&global_refcnt[index].lock);
}

int get_refcnt(uint64 pa) {
  int index = PA2IDX(pa);
  int refcnt;
  acquire(&global_refcnt[index].lock);
  refcnt = global_refcnt[index].count;
  release(&global_refcnt[index].lock);
  return refcnt;
}


void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
  ref_init();
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

  // Don't free if refcnt > 0
  if (ref_decr((uint64)pa) > 0) {
    return;
  }
  ref_reset((uint64)pa);

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

  if (r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    //ref_incr((uint64)r);
    int index = PA2IDX((uint64)r);
    global_refcnt[index].count = 1;
  }
  return (void*)r;
}
