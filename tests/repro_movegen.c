
#include "bitboard.h"
#include "magic.h"
#include "movegen.h"
#include "nnue.h"
#include "position.h"
#include "types.h"
#include <stdio.h>


// Function Declarations
void init_magic_tables();
void zobrist_init();

int main() {
  printf("Initializing...\n");
  init_magic_tables();
  zobrist_init();
  nnue_init();
  nnue_available = 1;

  Position pos;
  position_init(&pos);

  // FEN from reconstruction
  const char *fen = "r1b5/1pkn1Q2/2p5/p6p/7P/PPb3P1/5PB1/1R3R1K b - - 0 33";
  position_from_fen(&pos, fen);

  printf("FEN Loaded: %s\n", fen);

  // Valid setups
  int king_sq = LSB(pos.pieces[BLACK][KING]); // c7
  printf("Black King at: %d\n", king_sq);

  // Construct move d7f8
  int from = 51;
  int to = 61;
  Move move = MAKE_MOVE(from, to, 0, 0);

  printf("Testing movegen_is_legal for d7f8 (from %d to %d)...\n", from, to);

  int is_legal = movegen_is_legal(&pos, move);
  printf("movegen_is_legal returned: %d\n", is_legal);

  if (is_legal == 0) {
    printf("SUCCESS: Move rejected as illegal.\n");
  } else {
    printf("FAILURE: Move accepted as legal (BUG FOUND).\n");
  }

  return 0;
}
