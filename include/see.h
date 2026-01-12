#ifndef SEE_H
#define SEE_H

#include "position.h"
#include "types.h"

// Returns the static exchange evaluation score for a move.
// Positive score indicates a winning exchange, negative a losing one.
Score see(const Position *pos, Move move);

// Checks if a move is "safe" based on SEE and a margin.
int see_ge(const Position *pos, Move move, Score threshold);

#endif // SEE_H
