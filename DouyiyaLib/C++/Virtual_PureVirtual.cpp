#include "Virtual_PureVirtual.h"

std::string
File::getName(void) const {
  return FileName;
}

std::vector<File*>
File::getChildren(void) const {
  return Children;
}


int main() {
  VFile* root = new VFile();
  return 0;
}