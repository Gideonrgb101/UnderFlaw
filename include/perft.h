#ifndef PERFT_H
#define PERFT_H

#include "position.h"
#include <stdint.h>

// Initialize perft resources if any
void perft_init();

// Run perft for a specific depth and return leaf node count
uint64_t perft(Position *pos, int depth);

// Run perft with breakdown of moves at root
void perft_divide(Position *pos, int depth);

// Run a suite of tests from an EPD file
void run_test_suite(const char *filename);

// Bench command to measure speed
void run_bench(int depth);

#endif // PERFT_H
