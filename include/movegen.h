#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "position.h"
#include "types.h"

typedef struct {
  Move moves[256];
  int count;
} MoveList;

// Attack lookup tables (defined in movegen.c, exposed for SEE)
extern Bitboard knight_attacks[64];
extern Bitboard king_attacks[64];

void movegen_all(const Position *pos, MoveList *list);
void movegen_captures(const Position *pos, MoveList *list);
void movegen_quiet(const Position *pos, MoveList *list);
int movegen_is_legal(const Position *pos, Move move);
int movegen_is_pseudo_legal(const Position *pos, Move move);

#endif // MOVEGEN_H
