#include <iostream>
#include <filesystem>
#include <sys/stat.h>
#include <string.h>
 
// namespace fs = std::filesystem;
// clang++ fs.cpp -o main --std=c++17 -lstdc++fs

int setFileStats(const std::string& Path) {
  struct stat stat_buffer;

  if (lstat(Path.c_str(), &stat_buffer) != 0) {
    std::cout << Path
              << ": Failed to get file statistics!"
              << std::endl;
    return 1;
  }

  if (S_ISREG(stat_buffer.st_mode))
    std::cout << "regular file" << std::endl;
  else if (S_ISDIR(stat_buffer.st_mode)) {
    std::cout << " dir file" << std::endl;
  } else if (S_ISLNK(stat_buffer.st_mode))
    std::cout << "link file" << std::endl;
  else {
    std::cout << "no type" << std::endl;
  }

  uint64_t Size = stat_buffer.st_size,
  LinksCount = stat_buffer.st_nlink,
  AccessTime = static_cast<uint64_t>(stat_buffer.st_atim.tv_sec),
  ChangeTime = static_cast<uint64_t>(stat_buffer.st_ctim.tv_sec),
  ModifyTime = static_cast<uint64_t>(stat_buffer.st_mtim.tv_sec),
  BirthTime = static_cast<uint64_t>(stat_buffer.st_mtim.tv_sec);

  std::cout << "Size is " << Size << '\n'
            << "LinksCount is " << LinksCount << '\n'
            << "AccessTime is " << AccessTime << '\n'
            << "ChangeTime is " << ChangeTime << '\n'
            << "ModifyTime is " << ModifyTime << '\n'
            << "BirthTime is " << BirthTime << '\n'
            << std::endl;
  return 0;
}

int main(int argc, char **argv)
{
  // for(fs::path p : {"/usr/bin/gcc", "/bin/cat", "/bin/mouse"}) {
  //   std::cout << p;
  //   fs::exists(p)
  //     ? fs::is_symlink(p)
  //     ? std::cout << " -> " << fs::read_symlink(p) << '\n'
  //     : std::cout << " exists but it is not a symlink\n"
  //     : std::cout << " does not exist\n";
  // }
  
  std::string file_path(argv[1]);
  // fs::path p(file_path);
  // std::string target(fs::read_symlink(p).string());

  // std::cout << target << '\t' 
  //           << "size is " << target.size() 
  //           << std::endl;
  setFileStats(file_path);

  return 0;
}
