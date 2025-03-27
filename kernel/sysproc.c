#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
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

  backtrace();

  return 0;
}

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

// Force a call to function fn every n ticks and then spin.
uint64
sys_sigalarm(void) {
  // int, void (*fn)()
  /*
  uint now;
  acquire(&tickslock);
  now = ticks;
  myproc()->ticks_passed = now - myproc()->ticks;
  myproc()->ticks = now;
  printf("ticks and ticks_passed: %d, %d\n", myproc()->ticks, myproc()->ticks_passed);
  release(&tickslock);
  */
  // grab interval and fn from user space
  //
  uint64 now, fn;
  if (argaddr(0, &now) < 0 || argaddr(1, &fn) < 0) {
    return -1;
  }
  //printf("now and fn: %d %d\n", now, fn);

  struct proc *p = myproc();
  p->ticks = now;
  p->ticks_passed = now;
  p->fn = (void*)fn;
  // running already init to 0 in proc

  //printf("hi from sys_alarm\n");
  return 0;
}

uint64
sys_sigreturn(void) {
  // What registers do we need to save/restore resume of interrupted code?
  // A: All registers (user registers) we saved in trampoline. These are 
  // stored in trapframe, so we'll need to copy this into proc to save this state.
  struct proc *p = myproc();
  //*(p->resume_intr) = *(p->trapframe);
  // We are restoring the old trapframe (i.e., what resume_intr is pointing to)
  if (p->running == 1) {
    *(p->trapframe) = *(p->resume_intr);
    p->running = 0;
  }
  //backtrace();
  return 0;
}
