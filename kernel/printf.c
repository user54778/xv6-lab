//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
  struct spinlock lock;
  int locking;
} pr;

static char digits[] = "0123456789abcdef";

static void
printint(int xx, int base, int sign)
{
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}

static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
printf(char *fmt, ...)
{
  va_list ap;
  int i, c, locking;
  char *s;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  if (fmt == 0)
    panic("null fmt");

  va_start(ap, fmt);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&pr.lock);
}

void
panic(char *s)
{
  pr.locking = 0;
  printf("panic: ");
  printf(s);
  printf("\n");
  backtrace();
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}

// Prints out a list of function calls on the stack above the point
// at which an error occurred.
void
backtrace() {
  // * What we know -> Stack frame has a frame ptr holds addr of caller's fp.
  // * Frame ptr is stored in reg s0 of curr exec func.
  // * One page per stack -> top and bottom computed 
  //   via PGROUNDDOWN(fp) and PGROUNDUP(fp) respectively.
  // * General algorithm: 
  //    PGROUNDDOWN(fp) != fp
  //    start at PGROUNDUP(fp) (or vice versa?)
  //    while (current frame pointer != 0) {
  //      grab stack frame
  //      grab frame pointer (top of current frame)
  //      print saved return address
  //      move to next stack frame 
  //    }
  /*
  uint64 fp = r_fp();
  uint64 bot = PGROUNDDOWN(fp);
  uint64 top = PGROUNDUP(fp);

  printf("Current frame pointer: %d\n", fp);
  printf("Bottom of stack frame page: %d\n", bot);
  printf("Top of stack frame page: %d\n", top);

  printf("Return address %d\n", fp - 8);
  printf("To previous frame %d\n", fp - 16);
  */
  uint64 fp = r_fp(); 
  uint64 top = PGROUNDUP(fp);
  uint64 bot = PGROUNDDOWN(fp);

  printf("backtrace:\n");

  while (fp >= bot && fp < top) {
    // Crux of problem: We need the saved return address.
    // We get that the memory location of that with the fp - 8. However,
    // we need to make this a pointer to grab the actual VALUE at this memory address,
    // so we can dereference that pointer.
    uint64 savedAddr = *(uint64*)(fp - 8);
    // Now grab its value
    printf("%p\n", savedAddr); 
    fp = *((uint64*)(fp - 16));
  }
}

