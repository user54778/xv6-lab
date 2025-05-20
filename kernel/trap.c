#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"

struct spinlock tickslock;
uint ticks;

static void * is_cow_page(pagetable_t, uint64); 

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if (r_scause() == 15) {
    // Allocate new page with kalloc
    // Copy old page into new page
    // Install new page in PTE with PTE_W set.
    printf("trapped to cow page\n");
    //uint64 va = r_stval();
    // Is the faulting va a COW page?
    // If it is, allocate a new page with kalloc, cp the old pg into new pg,
    // install new pg in pte with w-bit.
    uint64 va = r_stval();
    pte_t *pte;
    if ((pte = is_cow_page(p->pagetable, va)) <= 0) {
      p->killed = 1;
    } else if (cow_alloc(pte) != 0) {
      p->killed = 1;
    }
    /*
    pte_t *pte;
    if ((pte = is_cow_page(p->pagetable, va)) <= 0) {
      p->killed = 1;
    } else if ((pte = cow_alloc(pte)) == 0) {
      p->killed = 1;
    }
    printf("PTE: %p\n", *pte);
    */
    /*
    int cow_ret = is_cow_page(p->pagetable, va);
    if (cow_ret < 0) {
      p->killed = 1;
    } else if (cow_ret == 0) {
      panic("usertrap: writable page on store pagefault");
    } else {
      pte_t *pte = walk(p->pagetable, va, 0);
      if (cow_alloc(pte) == 0) {
        p->killed = 1;
      }
    }
    */
    /*
    if (is_cow_page(p->pagetable, va) != 1) {
      p->killed = 1;
    } else {
      pte_t *pte = walk(p->pagetable, va, 0);
      if (cow_alloc(pte) == 0) {
        p->killed = 1;
      }
    }
    */
    /*
    } else if ((pa = (uint64)kalloc()) == 0) {
      p->killed = 1;
    } else {
      printf("We're a COW page, and we've kalloc'ed!\n");
      //
      // Copy the old page into the new page
      // Install the new page in the PTE with PTE_W set
      // Free page/decr refcnt to old page
    }
    */
    p->killed = 1;

  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}


//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

int
cow_alloc(pte_t *pte) {
  // check validity of pte as sanity check
  if (*pte == 0 || (*pte & PTE_V) == 0) {
    return -1;
  }
  if ((*pte & PTE_C) == 0) {
    return -1;
  }
  printf("PTE_C: %d\n", *pte & PTE_C);
  uint64 old_pa = PTE2PA(*pte); 

  char *mem;
  if ((mem = kalloc()) == 0) {
    return -1;
  }

  memmove(mem, (char*)old_pa, PGSIZE);
  kfree((void*)old_pa);

  *pte &= ~PTE_C;
  *pte |= PTE_W;
  *pte = PA2PTE(old_pa) | PTE_FLAGS(*pte);

  return 0;
}

// Determine if a given virtual address from a pagetable
// is a COW page.
// Returns -1 if the page does not exist or error.
// Returns 0 if the page exists but is NOT a COW page.
// Return the pte if its a cow page
static void *
is_cow_page(pagetable_t pagetable, uint64 va) {
  pte_t *pte;

  if (va >= MAXVA) {
    return (void*)-1;
  }
  va = PGROUNDDOWN(va);

  if ((pte = walk(pagetable, va, 0)) == 0) {
    return (void*)-1;
  }

  // Check page existence
  if ((*pte & PTE_V) == 0) {
    return (void*)-1;
  }
  // Check if a COW page
  if ((*pte & PTE_C) && !(*pte & PTE_W)) {
    printf("valid cow page\n");
    return pte;
  }

  return 0;
}
