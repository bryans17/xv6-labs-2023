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

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};

struct kmem cpu_freelist[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; ++i) {
    initlock(&cpu_freelist[i].lock, "kmem");
    cpu_freelist[i].freelist = 0;
  }
  freerange(end, (void*)PHYSTOP);
}


// should give all memory to the cpu running free range

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
  // reset mem of other cpu to empty?
}

// Free the page of physical memory pointed at by pa,
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
  push_off();
  int cpu_id = cpuid();
  pop_off();
  acquire(&cpu_freelist[cpu_id].lock);
  r->next = cpu_freelist[cpu_id].freelist;
  cpu_freelist[cpu_id].freelist = r;
  release(&cpu_freelist[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int cpu_id = cpuid();
  pop_off();
  acquire(&cpu_freelist[cpu_id].lock);
  r = cpu_freelist[cpu_id].freelist;
  if(r) {
    cpu_freelist[cpu_id].freelist = r->next;
    release(&cpu_freelist[cpu_id].lock);
  } else {
    // steal if available
    // avoid deadlock
    release(&cpu_freelist[cpu_id].lock);
    for(int i = 0; i < NCPU; ++i) {
      if(i == cpu_id) continue;
      acquire(&cpu_freelist[i].lock);
      r=cpu_freelist[i].freelist;
      if(r) {
        cpu_freelist[i].freelist = r->next;
        r->next = 0;
        release(&cpu_freelist[i].lock);
        break;
      }
      release(&cpu_freelist[i].lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
