#ifndef _H_CPP_TEST_H_
#define _H_CPP_TEST_H_

#include <string>

namespace CppTestCase {

class TestFriend {
public:
  TestFriend() = default;
  TestFriend(int value);
  ~TestFriend() = default;

public:
  void free(TestFriend* tf);
  int getNumber(void) const;
  void setNumber(int value);
  void print(void);

private:
  int number;
};

} // namespace CppTestCase

#endif