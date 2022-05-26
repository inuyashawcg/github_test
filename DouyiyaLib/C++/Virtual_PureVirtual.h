#ifndef _VIRTUAL_PURE_VIRTUAL_H_
#define _VIRTUAL_PURE_VIRTUAL_H_

#include <string>
#include <iostream>
#include <vector>

class File {
public:
  File() {}
  virtual ~File() {}

public:
  virtual std::string getName(void) const = 0;
  std::vector<File*> getChildren(void) const;
  void insert(File*);

private:
  std::string FileName;
  std::vector<File*> Children;
};


class VFile : public File {
public:
  VFile() {}
  virtual ~VFile() {}

public:
  VFile* search(const std::string& Name) const;
  virtual std::string getName(void) const override;
private:
  Children
};

VFile vfile;
VFile vfile_child;
vfile.insert(vfile_child);
for(auto v : vfile.children) {
  cout << getName(v);
}
#endif // _VIRTUAL_PURE_VIRTUAL_H_