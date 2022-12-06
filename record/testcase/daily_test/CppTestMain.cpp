#include "CppTest.h"

using namespace CppTestCase;

int main() {
  TestFriend* a = new TestFriend(1);
  a->print();
  a->setNumber(0);
  a->free(a);
  a->print();

  return 0;
}