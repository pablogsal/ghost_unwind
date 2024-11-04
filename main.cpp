#include "shadow_stack.hpp"
#include <iostream>

__attribute__((noinline))
void function3() {
    std::cout << "In function3, capturing stack trace..." << std::endl;
    ShadowStack::get().capture_stack_trace();
    std::cout << "Stack trace captured, returning..." << std::endl;
}

__attribute__((noinline))
void function2() {
    std::cout << "In function2" << std::endl;
    function3();
    std::cout << "Back in function2" << std::endl;  // This will go through trampoline
}

__attribute__((noinline))
void function1() {
    std::cout << "In function1" << std::endl;
    function2();
    std::cout << "Back in function1" << std::endl;  // This will go through trampoline
}

int main() {
    std::cout << "In main" << std::endl;
    function1();
    std::cout << "Back in main" << std::endl;  // This will go through trampoline
    return 0;
}