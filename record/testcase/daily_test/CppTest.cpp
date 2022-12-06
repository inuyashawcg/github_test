#include <iostream>
#include "CppTest.h"

namespace CppTestCase {

TestFriend::TestFriend(int value) : number(value) {
}

void TestFriend::free(TestFriend* tf) {
  int num = tf->getNumber();

  if (num == 0) {
    delete tf;
    std::cout << "Target object has been freed." << std::endl;
  } else
    std::cout << "Target object is not freed." << std::endl;
  print();
}

int TestFriend::getNumber(void) const {
  return number;
}

void TestFriend::setNumber(int value) {
  number = value;
}

void TestFriend::print(void) {
  std::cout << "Number is " << number << std::endl;  
}

} // namespace CppTestCase
