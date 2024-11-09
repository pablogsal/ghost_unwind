#!/bin/bash
as trampoline.S -o trampoline.o
g++ -c ghost_stack.cpp -o ghost_stack.o -g -O0
g++ -c main.cpp -o main.o -g -O0
g++ trampoline.o ghost_stack.o main.o -o ghost_stack_test -lunwind
