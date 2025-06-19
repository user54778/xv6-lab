#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  // Allocate region of memory for receive descriptor list,
  // aligned on a 16-byte boundary
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  
  // What should TX do?
  // Transmission process goes as so:
  // 1) Protocol stack recvs block of data to transmit.
  // 2) Protocol stack computes num packets to transmit
  // 3) For each packet:
  //    — Ethernet, IP and TCP/UDP headers are prepared by the stack.
  //    — The stack interfaces with the software device driver and commands the driver to send the
  //      individual packet.
  //    — The driver gets the frame and interfaces with the hardware.
  //    — The hardware reads the packet from host memory (via DMA transfers).
  //    — The driver returns ownership of the packet to the Network Operating System (NOS) when
  //    the hardware has completed the DMA transfer of the frame (indicated by an interrupt).
  // 
  // Output packets are made up of pointer-length pairs, software should
  // transmit packets by assembling the list of pointer-length pairs, by storing
  // this info in the transmit descriptor, and then updating the on-chip transmit TAIL 
  // pointer to the descriptor.
  //
  // We can interact with the E1000 via mmap'd ctl regs to inform when tx descriptors
  // are filled to send.
  // regs holds a pointer to the FIRST E1000 ctl reg.
  // E1000_TDT is what holds the tail pointer of the ring buffer for tx.
  //
  // NOTE: We need locks to cope with possibility of e1000 being used in multiple processes.
  acquire(&e1000_lock);

  // Index into the regs array with 
  uint32 tail_index = regs[E1000_TDT];
  uint32 head_index = regs[E1000_TDH];
  printf("tail, head %d %d\n", tail_index, head_index);
  // 1) Need to check if ring is overflowing
  //    a) Hardware uses the head to process descriptors. How can we compare the two?
  //    b) We know it is a circular buffer. Check if the hardware head + 1 modulo ring size
  //    is the same as where the tail index is pointing (which is + 1 of current tail)
  // FIXME: Possible bug source such that software maybe shouldn't be touching
  // the head?
  if ((head_index + 1) % TX_RING_SIZE == tail_index) {
    //panic("e1000_transmit(): tx ring overflow");
    release(&e1000_lock);
    return -1;
  }
  // 2) Also need to check if DD is set
  if (!(tx_ring[tail_index].status & E1000_TXD_STAT_DD)) {
    //panic("e1000_transmit(): failed to finish previous request");
    release(&e1000_lock);
    return -1;
  }
  // 3) Use mbuffree() to free the last mbuf transmitted from that descriptor, if 
  // there was one.
  if (tx_mbufs[tail_index] != 0) {
    mbuffree(tx_mbufs[tail_index]);
    tx_mbufs[tail_index] = 0; // null out
  }

  // mbuf works as so: 
  //
  // Bit operations for our purpose as so:
  //
  // 4) Fill in the descriptor using 3.3.
  //    m->head pts to the packet's content in memory 
  //    m->len is packet len
  //    Set the correct cmd flags
  //    "Stash away a pointer to the mbuf to later free"
  //      -> Stash away in this context means storing the pointer to the mbuf
  //      somewhere such that it can be later cleaned up.
  tx_mbufs[tail_index] = m; // stash away pointer
  tx_ring[tail_index].addr = (uint64)tx_mbufs[tail_index]->head; // addr -> mbuf content
  tx_ring[tail_index].length = m->len;
  // We have EOP and RS bits given to us as macros
  tx_ring[tail_index].cmd |= (E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP);
  // 5) Update ring position.
  printf("prior ring pos: %d\n", regs[E1000_TDT]);

  regs[E1000_TDT] = (tail_index + 1) % TX_RING_SIZE;

  printf("updated ring pos: %d\n", regs[E1000_TDT]);

  printf("tx_ring addr: %p\n", tx_ring[tail_index].addr);
  printf("tx_ring len: %d\n", tx_ring[tail_index].length);
  printf("tx_ring cmd bits: %d\n", tx_ring[tail_index].cmd);
  //
  // Additional: If we were able to add the mbuf to the ring successfully, return 0.
  // Otherwise, return -1 so the *caller* knows to free mbuf.

  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  printf("Hello from e1000_recv\n");
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
