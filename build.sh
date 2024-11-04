#!/bin/bash
as trampoline.S -o trampoline.o
g++ -c shadow_stack.cpp -o shadow_stack.o -g -O0
g++ -c main.cpp -o main.o -g -O0
g++ trampoline.o shadow_stack.o main.o -o shadow_stack_test -lunwind
