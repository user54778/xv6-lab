// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024   // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
  short type;              // File type
  short major;             // Major device number (T_DEVICE only)
  short minor;             // Minor device number (T_DEVICE only)
  short nlink;             // Number of links to inode in file system
  uint size;               // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
                           // Last entry gives address of indirect block
};

// Crux of the Problem: xv6 files are limited to 268 blocks, or 268 * BSIZE bytes, (12 + 256 = 268).
// The command bigfile expects us to be able to make a file that is 65803 blocks!
// We need to change xv6 to support a doubly-indirect block in each inode, which will each contain
// 256 addresses to singly-indirect blocks, which contains 256 addresses of data blocks.
// This results in 256*256+256+11 = 65803 blocks.

// To support a double indirect block, we need (NDIRECT - 1) + 2 = 13 still blocks.
// "If you change the definition of NDIRECT, you'll probably have to change the declaration of 
//    addrs[] in struct inode in file.h." 
// "Make sure that struct inode and struct dinode have the same number of elements in their addrs[] arrays."
// "If you change the definition of NDIRECT, make sure to create a new fs.img, since mkfs uses 
//    NDIRECT to build the file system."
//  Need to ensure itrunc frees *all* blocks of a file including our new doubly-indirect blocks.

// NDIRECT * BSIZE bytes are loaded from blocks in inode
// NINDIRECT * BSIZE bytes loaded after consulting indirect block.

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)
// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

// A directory is a tuple of (inode num, entry name) pairs.
// For each file/directory in a given directory, there is an associated number 
// in the data block(s) and string, which is at most DIRSIZ characters.
//
// Directory entries with an inode num of zero is empty/free.
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

