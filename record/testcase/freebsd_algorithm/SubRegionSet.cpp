#include <iostream>
#include <string>

#define PAGE_SIZE   4096
#define INODE_ENTRY_SIZE  256
#define DEFAULT_PAGES_PER_FILE  1
#define INODES_OCCUPIED 4

#define MB_TO_PAGE  ((1024 * 1024) / (PAGE_SIZE))
#define GB_TO_PAGE  ((MB_TO_PAGE) * (1024))
#define TB_TO_PAGE  ((GB_TO_PAGE) * (1024))

// algorithm of setSubAreas():
//  @fs_pages: Total number of virtual pages used by the file system.
//  @ppf: average pages per file.
//  @ipp: inode entries per page.
//  @ps:  page size.
//
// Assume that the data area of tptfs occupies x pages. Then,
// the number of pages occupied by other subareas are:
//  superblock       1
//  data region      x
//  page bitmap      x / (ps * 8)
//  inode table      x / (ppf * ipp)
//  inode bitmap     x / (ppf * ps * 8)
// >>>
// (ppf*ipp*ps*8) + (ppf*ipp*ps*8)*x + (ppf*ipp)*x + (ps*8)*x + ipp*x
//    = (ppf*ipp*ps*8)*pages
// >>>
// x = ((ppf*ipp*ps*8)*(pages - 1)) / (ppf*ipp*ps*8 + ppf*ipp + ps*8 + ipp)
// 
// >>>
// page_bitmap_size = ((ppf*ipp)*(pages - 1)) / (ppf*ipp*ps*8 + ppf*ipp + ps*8 + ipp)
// inode_table_size = ((ps*8)*(pages - 1)) / (ppf*ipp*ps*8 + ppf*ipp + ps*8 + ipp)
// inode_bitmap_size = (ipp*(pages - 1)) / (ppf*ipp*ps*8 + ppf*ipp + ps*8 + ipp)
// 
// We must ensure that the number of pages occupied by block bitmap /
// inode bitmap / inode entry table are integers respectively.
// Therefore, we need to use x to obtain the sizes(in pages) of them,
// and finally calculate the size of the data area.
//
void setSubAreas(int FsSize, const std::string& UnitType) {
  uint64_t fs_pages = 0;
  if (!UnitType.compare("k") || !UnitType.compare("K")) {
    std::cout << "Please check rootfs image size value." << std::endl;
    return;
  } else if (!UnitType.compare("m") || !UnitType.compare("M")) {
    fs_pages = FsSize * MB_TO_PAGE;
    std::cout << "MB: filesystem pages is " << fs_pages << std::endl;
  } else if (!UnitType.compare("g") || !UnitType.compare("G")) {
    fs_pages = FsSize * GB_TO_PAGE;
    std::cout << "GB: filesystem pages is " << fs_pages << std::endl;
  } else {
    std::cout << "rootfs image size is too large" << std::endl;
    return;
  }

  uint32_t ppf = DEFAULT_PAGES_PER_FILE,
           ipp = PAGE_SIZE / INODE_ENTRY_SIZE,
           ps = PAGE_SIZE,
           page_bitmap_size,
           inode_bitmap_size,
           inode_table_size,
           data_region_size,
           inodes_count,
           inodes_free,
           pages_count,
           pages_free;

  uint64_t divisor = ppf*ipp*ps*8 + ppf*ipp + ps*8 + ipp;

  // get page bitmap size.
  if ((ppf * ipp * (fs_pages - 1)) % divisor != 0)
    page_bitmap_size = (ppf * ipp * (fs_pages - 1)) / divisor + 1;
  else
    page_bitmap_size = (ppf * ipp * (fs_pages - 1)) / divisor;
  
  // Get inode bitmap size.
  if ((ipp* (fs_pages - 1)) % divisor != 0)
    inode_bitmap_size = (ipp* (fs_pages - 1)) / divisor + 1;
  else
    inode_bitmap_size = (ipp* (fs_pages - 1)) / divisor;
  
  // Get inode table size.
  if ((ps * 8 * (fs_pages - 1)) % divisor != 0)
    inode_table_size = (ps * 8 * (fs_pages - 1)) / divisor + 1;
  else
    inode_table_size = (ps * 8 * (fs_pages - 1)) / divisor;
  
  // Get data region size;
  data_region_size = fs_pages - 1 - page_bitmap_size -
                                inode_bitmap_size -
                                inode_table_size;

  inodes_count = inode_table_size * ipp;
  inodes_free = inodes_count - INODES_OCCUPIED;
  pages_count = fs_pages;
  pages_free = data_region_size;

  std::cout << "page_bitmap_size = " << page_bitmap_size << '\n'
            << "inode_bitmap_size = " << inode_bitmap_size << '\n'
            << "inode_table_size = " << inode_table_size << '\n'
            << "inodes_count = " << inodes_count << '\n'
            << "inodes_free = " << inodes_free << '\n'
            << "pages_count = " << pages_count << '\n'
            << "pages_free = " << pages_free << '\n'
            << std::endl;
}

int main(int argv, char** argc) {
  std::string fs_size = argc[1], unit_type = argc[2];
  setSubAreas(std::stoi(fs_size), unit_type);

  return 0;
}