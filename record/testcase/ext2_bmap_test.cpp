#include <iostream>
#include <string>

#define	EXT2_NDADDR	15		/* Direct addresses in inode. */
#define	EXT2_NIADDR	3		/* Indirect addresses in inode. */
#define MNINDIR 512;

typedef int64_t e2fs_lbn_t;

struct indir {
	e2fs_lbn_t in_lbn;		/* Logical block number. */
	int	in_off;			/* Offset in buffer. */
};

int
ext2_getlbns(daddr_t bn, struct indir *ap, int *nump) {
  long blockcnt;
	e2fs_lbn_t metalbn, realbn;
  int i, numlevels, off;
	int64_t qblockcnt;

  if (nump)
		*nump = 0;
	numlevels = 0;
	realbn = bn;
	if ((long)bn < 0)
		bn = -(long)bn;

  if (bn < EXT2_NDADDR)
    return 0;

  for (blockcnt = 1, i = EXT2_NIADDR, bn -= EXT2_NDADDR; ;
	      i--, bn -= blockcnt) {
		if (i == 0)
			return (EFBIG);

    qblockcnt = (int64_t)blockcnt * MNINDIR;
    if (bn < qblockcnt)
        break;
    blockcnt = qblockcnt;
  }

  if (realbn >= 0)
    metalbn = -(realbn - bn + EXT2_NIADDR - i);
  else
    metalbn = -(-realbn - bn + EXT2_NIADDR - i);

  ap->in_lbn = metalbn;
	ap->in_off = off = EXT2_NIADDR - i;
	ap++;
	for (++numlevels; i <= EXT2_NIADDR; i++) {
		/* If searching for a meta-data block, quit when found. */
		if (metalbn == realbn)
			break;

		off = (bn / blockcnt) % MNINDIR;

		++numlevels;
		ap->in_lbn = metalbn;
		ap->in_off = off;
		++ap;

		/* metabln 可以看做是每个检查等级的 base address */
		metalbn -= -1 + off * blockcnt;
		blockcnt /= MNINDIR;
	}
	if (nump)
		*nump = numlevels;

	return (0);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cout << "please intput block number!" << std::endl;
    return -1;
  }
  std::string blocks_str = argv[1];
  long blocks = std::stol(blocks_str);

  struct indir a[EXT2_NIADDR + 1], *ap;
  int num, *nump, error, i;

  nump = &num;
  ap = a;
  for (i = 0; i < (EXT2_NIADDR + 1); i++) {
    a[i].in_lbn = 0;
    a[i].in_off = 0;
  }

  error = ext2_getlbns(blocks, ap, nump);
  if (error)
    std::cout << "ext2_getlbns() error" << std::endl;

  for (i = 0; i < (EXT2_NIADDR + 1); i++) {
    std::cout << "indir[" << i << "].in_lbn: " << a[i].in_lbn << '\t'
              << "indir[" << i << "].in_off:  " << a[i].in_off << '\n'
              << std::endl;
  }
  std::cout << "num is " << num << '\n'
            << "bn is " << blocks << std::endl;

  return 0;
}
