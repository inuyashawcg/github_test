#include <iostream>
#include <string>

#define	TPT_NDADDR	15		/* Direct addresses in inode. */
#define	TPT_NIADDR	3		/* Indirect addresses in inode. */
#define MNINDIR 512;

typedef int64_t tpt_addr_t;

struct tpt_indirect {
	tpt_addr_t logical_block;		/* Logical block number. */
	int	offset;			/* Offset in buffer. */
};

int getIndirectLevelNumber(tpt_addr_t BlockNum,
                                    struct tpt_indirect *Indirects,
                                    int *IndextLevel) {
  long block_count = 0;
  tpt_addr_t meta_logical_block = 0, real_block = 0, qblock_count = 0;
  int i = 0, indirect_level = 0, offset = 0;

  if (IndextLevel)
    *IndextLevel = 0;
  indirect_level = 0;
  real_block = BlockNum;

  if (BlockNum < 0)
    BlockNum = -BlockNum;

  // The first TPT_NDADDR blocks are direct blocks.
  if (BlockNum < TPT_NDADDR)
    return 0;

  // Determine the number of levels of indirection.  After this loop
	// is done, blockcnt indicates the number of data blocks possible
	// at the previous level of indirection, and TPT_NIADDR - i is the
	// number of levels of indirection needed to locate the requested block.
  for (block_count = 1, i = TPT_NIADDR, BlockNum -= TPT_NDADDR; ;
        i--, BlockNum -= block_count) {
    if (i == 0)
      return EFBIG;

    // Use int64_t's here to avoid overflow for triple indirect
    // blocks when longs have 32 bits and the block size is more
    // than 4k.
    qblock_count = (int64_t)block_count * MNINDIR;
    if (BlockNum < qblock_count)
      break;
    block_count = qblock_count;
  }

  // Calculate the address of the first meta-block.
  if (real_block >= 0)
    meta_logical_block = -(real_block - BlockNum + TPT_NIADDR - i);
  else
    meta_logical_block = -(-real_block - BlockNum + TPT_NIADDR - i);

  // At each iteration, off is the offset into the bap array which is
  // an array of page addresses at the current level of indirection.
  // The logical block number and the offset in that block are stored
  // into the argument array.
  Indirects->logical_block = meta_logical_block;
  Indirects->offset = offset = TPT_NIADDR - i;
  Indirects++;

  for (++indirect_level; i <= TPT_NIADDR; i++) {
    // If searching for a meta-data block, quit when found.
    if (meta_logical_block == real_block)
      break;

    offset = (BlockNum / block_count) % MNINDIR;

    ++indirect_level;
    Indirects->logical_block = meta_logical_block;
    Indirects->offset = offset;
    ++Indirects;
  
    meta_logical_block -= -1 + offset * block_count;
    block_count /= MNINDIR;
  }

  if (IndextLevel)
    *IndextLevel = indirect_level;
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cout << "please intput block number!" << std::endl;
    return -1;
  }
  std::string blocks_str = argv[1];
  long blocks = std::stol(blocks_str);

  struct tpt_indirect a[TPT_NIADDR + 1], *ap;
  int num, *nump, error, i;

  nump = &num;
  ap = a;
  for (i = 0; i < (TPT_NIADDR + 1); i++) {
    a[i].logical_block = 0;
    a[i].offset = 0;
  }

  error = getIndirectLevelNumber(blocks, ap, nump);
  if (error)
    std::cout << "getIndirectLevelNumber() error" << std::endl;

  for (i = 0; i < (TPT_NIADDR + 1); i++) {
    std::cout << "tpt_indirect[" << i << "].logical_block: " << a[i].logical_block << '\t'
              << "tpt_indirect[" << i << "].offset:  " << a[i].offset << '\n'
              << std::endl;
  }
  std::cout << "num is " << num << std::endl;

  return 0;
}