#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"


// NOTE: stvec    -> Where the kernel writes the address of its trap handler
// NOTE: sepc     -> Where RISC-V saves the program counter. `sret` copies `sepc` to the pc. 
//                   The kernel can write to `sepc` to control where `sret` goes. 
// NOTE: scause   -> Reason for the trap
// NOTE: sscratch -> Kernel uses for start of TRAMPOLINE.
// NOTE: sstatus  -> SIE bit in `sstatus` controls whether device interrupts are enabled.
//                   SSP bit indicates whether trap came from user/supervisor, and controls 
//                   what mode `sret` returns.
//
// The path of a trap from user space goes as follows:
// uservec (trampoline.S) -> usertrap (trap.c) 
// -> usertrapret (trap.c) calls userret -> userret (trampoline.S).

struct spinlock tickslock;
uint ticks;

// strings to place in trampoline, uservec, and userret.
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

// Handle an interrupt, exception, or system call from user space.
// Called from trampoline.S.
void
usertrap(void)
{
  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0) {
    panic("usertrap: not from user mode");
  }

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  // grab currently running process
  struct proc *p = myproc();
  
  // save user program counter; we might switch into ANOTHER process
  // while executing here, so save sepc.
  p->trapframe->epc = r_sepc();
  
  // Check reason for trap.
  // SSP bit; 8 means trap came from user mode, i.e., 
  // we came here because of a system call.
  // Figure 10.3.
  if (r_scause() == 8) {
    // system call
    if (p->killed) {
      exit(-1);
    }

    // sepc points to the ecall instruction,
    // but we want to return to the NEXT instruction.
    // We don't want to re-execute the sepc call.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    // Look up syscall number and perform that system call. Then 
    // start the return back to user space.
    syscall();
  } else if ((which_dev = devintr()) != 0) {
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if (p->killed) {
    exit(-1);
  }

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2) {
    // Manipulate the proc's alarm ticks here
    // Only invoke if timer outstanding (what does that mean)?
    // When a alarm interval expires, execute handler
    // C: We know p->trapframe->epc is currently holding sepc, which points to 
    // the user pc, which would let us execute the user code handler.
    // B: we can determine this with ticks_passed and ticks.
    p->ticks_passed++;
    // Timer is outstanding, i.e., ticks_passed has now reached the interval (or greater)
    if (p->ticks_passed >= p->ticks) {
      // We should now reset ticks_passed to reflect this.
      p->ticks_passed = 0;
      // And we should now point the user pc to the handler function.
      p->trapframe->epc = (uint64)p->fn;
    }
    yield();
  }

  // Go back to user space (start to).
  usertrapret();
}

// Return to user space.
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
  p->trapframe->kernel_trap = (uint64)usertrap; // usertrap function
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  // setup various bits in sstatus register.
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
  // Cast `fn` to a function pointer, which takes two uint64 and returns void.
  // It then calls this function pointer with TRAPFRAME and satp.
  // Use fn as a function pointer and then jump to that function with TRAPFRAME and satp
  // args in a0 and a1.
  ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
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

