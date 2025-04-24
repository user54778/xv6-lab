// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define NULL ((void *)0)

void freerange(void *pa_start, void *pa_end);
int get_cpuid(); 
struct run *steal(int);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];


void
kinit()
{
  char buf[100];
  for (int i = 0; i < NCPU; i++) {
    snprintf(buf, sizeof(buf), "kmem_%d", i);
    initlock(&kmem[i].lock, buf); 
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kfree(p);
  }
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int cpu = get_cpuid();

  if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP) {
    panic("kfree");
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // NOTE: give all free memory to cpuid()
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
//
// The allocator uses a DS named a free list of physical memory pages that
// are available for allocation.
void *
kalloc(void)
{
  // A free page's list element
  struct run *r;
  int cpu = get_cpuid();

  // NOTE: See how there is a single lock protecting the ENTIRE list?
  // This will result in high lock contention on multi-core CPU systems,
  // since multiple cores will be vying for this very frequent operation.
  // The same idea applies for kfree.
  acquire(&kmem[cpu].lock);
  // Point to the first element in the free list
  r = kmem[cpu].freelist;
  // If the free list is not NULL, point to the *next* element in the free list.
  if (r) {
    kmem[cpu].freelist = r->next;
  } else {
    r = steal(cpu);
  }
  release(&kmem[cpu].lock);

  // Initialize the element with junk
  if (r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}

// TODO: This is slow. If a CPU needs multiple pages, it will need to 
// re-run this function page times, which will result in unnecessary locking/unlocking.
// A more efficient version will steal *all* the pages it needs at once and reflect that 
// in the allocator (kalloc).
struct run *
steal(int cpu) {
  struct run *r = NULL;
  // iterate over the cpus
  // attempt to steal an element from another cpus free list
  // How would we do this?
  // Simply loop until we find a free element, and point to the next element of that free list
  // We would then need to mark that we used this element (simply set to NULL)
  // If we find no list, return null.
  for (int i = 0; i < NCPU; i++) {
    if (i == cpu) {
      continue;
    }

    acquire(&kmem[i].lock);
    r = kmem[i].freelist;
    // found free element
    if (r) {
      kmem[i].freelist = r->next;
      // mark element as used (null)
      r->next = NULL;
      release(&kmem[i].lock);
      return r;
    }
    release(&kmem[i].lock);
  }
  // will be null
  return r;
}

// Grab the cpu_id
int get_cpuid() {
  push_off();
  int id = cpuid();
  pop_off();
  return id;
}
