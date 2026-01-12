#ifndef MAGIC_H
#define MAGIC_H

#include "bitboard.h"

// Magic numbers for rooks and bishops
extern uint64_t ROOK_MAGICS[64];
extern uint64_t BISHOP_MAGICS[64];

extern Bitboard *ROOK_ATTACKS[64];
extern Bitboard *BISHOP_ATTACKS[64];

extern int ROOK_SHIFTS[64];
extern int BISHOP_SHIFTS[64];

// Pre-computed attack masks
extern Bitboard ROOK_MASKS[64];
extern Bitboard BISHOP_MASKS[64];

extern Bitboard knight_attacks[64];
extern Bitboard king_attacks[64];

void init_magic_tables();
void deinit_magic_tables();

// Direct ray calculation (more reliable than broken magic numbers)
static inline Bitboard get_rook_attacks(int sq, Bitboard occupied) {
  Bitboard attacks = 0;
  int rank = sq >> 3;
  int file = sq & 7;

  // Right
  for (int f = file + 1; f < 8; f++) {
    attacks |= 1ULL << (rank * 8 + f);
    if (occupied & (1ULL << (rank * 8 + f)))
      break;
  }
  // Left
  for (int f = file - 1; f >= 0; f--) {
    attacks |= 1ULL << (rank * 8 + f);
    if (occupied & (1ULL << (rank * 8 + f)))
      break;
  }
  // Up
  for (int r = rank + 1; r < 8; r++) {
    attacks |= 1ULL << (r * 8 + file);
    if (occupied & (1ULL << (r * 8 + file)))
      break;
  }
  // Down
  for (int r = rank - 1; r >= 0; r--) {
    attacks |= 1ULL << (r * 8 + file);
    if (occupied & (1ULL << (r * 8 + file)))
      break;
  }

  return attacks;
}

static inline Bitboard get_bishop_attacks(int sq, Bitboard occupied) {
  Bitboard attacks = 0;
  int rank = sq >> 3;
  int file = sq & 7;

  // NE
  for (int r = rank + 1, f = file + 1; r < 8 && f < 8; r++, f++) {
    attacks |= 1ULL << (r * 8 + f);
    if (occupied & (1ULL << (r * 8 + f)))
      break;
  }
  // NW
  for (int r = rank + 1, f = file - 1; r < 8 && f >= 0; r++, f--) {
    attacks |= 1ULL << (r * 8 + f);
    if (occupied & (1ULL << (r * 8 + f)))
      break;
  }
  // SE
  for (int r = rank - 1, f = file + 1; r >= 0 && f < 8; r--, f++) {
    attacks |= 1ULL << (r * 8 + f);
    if (occupied & (1ULL << (r * 8 + f)))
      break;
  }
  // SW
  for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; r--, f--) {
    attacks |= 1ULL << (r * 8 + f);
    if (occupied & (1ULL << (r * 8 + f)))
      break;
  }

  return attacks;
}

static inline Bitboard get_queen_attacks(int sq, Bitboard occupied) {
  return get_rook_attacks(sq, occupied) | get_bishop_attacks(sq, occupied);
}

#endif // MAGIC_H
