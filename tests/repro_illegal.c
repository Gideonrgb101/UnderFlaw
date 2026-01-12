
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

// Mocks removed to link real NNUE.c
// But we need to use 'nnue_available' from nnue.c
#include "nnue.h"

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

  // Validate setup
  int king_sq = LSB(pos.pieces[BLACK][KING]); // c7
  printf("Black King at: %d\n", king_sq);

  // Construct move d7f8
  // d7=51, f8=61
  // Wait. SQ(file, rank)
  // d7: file 3, rank 6 -> 6*8+3=51.
  // f8: file 5, rank 7 -> 7*8+5=61.
  int from = 51;
  int to = 61;
  Move move = MAKE_MOVE(from, to, 0, 0); // Promotion=0, Special=0

  printf("Making move d7f8 (from %d to %d)...\n", from, to);

  UndoInfo undo;
  position_make_move(&pos, move, &undo);

  // Now checks
  // We need to verify if Black is in check.
  // position_make_move flips side to move. So pos.to_move is WHITE.
  // To check if Black is in check, we flip it back.
  pos.to_move = BLACK;

  int in_check = position_in_check(&pos);
  printf("position_in_check(BLACK) = %d\n", in_check);

  if (in_check) {
    printf("SUCCESS: Engine knows it is in check.\n");
  } else {
    printf("FAILURE: Engine thinks it is SAFE.\n");

    // Debug
    Bitboard occupied = pos.all;
    printf("Occupied BB: %llx\n", occupied);
    int piece_d7 = (occupied >> 51) & 1;
    printf("Is d7 occupied? %d\n", piece_d7);

    Bitboard attacks = get_rook_attacks(king_sq, occupied);
    printf("Rook Attacks from c7(%d): %llx\n", king_sq, attacks);

    Bitboard queen = pos.pieces[WHITE][QUEEN];
    printf("White Queens: %llx\n", queen);

    if (attacks & queen) {
      printf("Output says ATTACKED but function returned 0?\n");
    }
  }

  return 0;
}
