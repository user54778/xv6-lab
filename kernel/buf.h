// Represent a buffer for the buffer cache layer in the file system.
struct buf {
  // Indicate if the buffer contains a copy of the block. 
  // Or, has data been read from disk?
  int valid;              
  // Indicate if the buffer content had been handed to disk, 
  // which may write data from disk *into* data.
  // Or does disk "own" buf?
  int disk;               
  // device
  uint dev;               
  // block number
  uint blockno;           
  // a sleeplock to protect this buffer (I/O takes a long time)
  struct sleeplock lock;  
  // how many references the system has to this buffer
  uint refcnt;            
  // LRU cache list
  struct buf *prev;       
  // next pointer to buffer
  struct buf *next;       
  // time of last use
  uint timestamp;         
  // actual data in this buffer
  uchar data[BSIZE];      
};

