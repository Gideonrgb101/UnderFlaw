#ifndef NNUE_H
#define NNUE_H

#include "position.h"
#include "types.h"
#include <stdint.h>

#include "position.h"
#include "types.h"
#include <stdint.h>

// Global API
void nnue_init();
int nnue_load(const char *filename);
int nnue_load_embedded();
int nnue_evaluate(const Position *pos);
void nnue_refresh_accumulator(const Position *pos); // Full refresh
void nnue_update_accumulator(Accumulator *acc, int piece, int color, int sq,
                             int is_add, int w_king,
                             int b_king); // Incremental update

// Feature transformer helper (incremental updates usually handled in
// search/do_move, but for simplicity v1 might just refresh or do naive
// updates).

extern int nnue_available;

#endif // NNUE_H
