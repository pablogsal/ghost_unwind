#include "ghost_stack.hpp"
#include <exception>
#include <iostream>

__attribute__((noinline)) int function3() {
  std::cout << "In function3, capturing stack trace..." << std::endl;
  GhostStack::get().unwind(true);
  // std::cout << "Stack trace captured..." << std::endl;
  const auto &ex = std::exception();
  // std::cerr << "Exception addr: " << (const void *)&ex << std::endl;
  GhostStack::get().unwind();
  // throw ex;
  // std::cout << "Second Stack trace captured..." << std::endl;
  return 42;
}

__attribute__((noinline)) int function2() {
  std::cout << "In function2" << std::endl;
  int res = function3();
  std::cout << "Back in function2"
            << std::endl; // This will go through trampoline
  int other = 12;
  int other2 = 12;
  int other3 = 12;
  return res + 1;
}

__attribute__((noinline)) int function1() {
  std::cout << "In function1" << std::endl;
  int res = function2();
  std::cout << "Back in function1"
            << std::endl; // This will go through trampoline
  return res + 1;
}

int main() {
  std::cout << "In main" << std::endl;
  int res = 0;
  try {
    std::cout << "res:" << res << std::endl; // This will go through trampoline
    res = function1();
  } catch (...) {
    std::cout << "Recovered!" << std::endl; // This will go through trampoline
  }
  std::cout << "Back in main" << std::endl; // This will go through trampoline
  std::cout << "Result: " << res
            << std::endl; // This will go through trampoline
  return 0;
}
