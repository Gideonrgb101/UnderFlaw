#include "magic.h"
#include <stdlib.h>
#include <string.h>

// Pre-computed magic numbers (derived from known sources)
uint64_t ROOK_MAGICS[64] = {
    0xa8002c000108020,  0x6c00049b0002001,  0x100080010040002,
    0x2080504008010200, 0x1040200010008080, 0x8000400020005000,
    0x804001002000,     0x12000c00008000,   0x2000800100080004,
    0x1000200040100040, 0xa004008180002000, 0x940000c80048001,
    0x239001000100400,  0x1000140010000100, 0x40080010001000c0,
    0x58080014000800,   0x804000800100,     0x6001000200040,
    0x1000200080100080, 0x500040008008020,  0x1000a00200040,
    0x430000a044020001, 0x280009000100801,  0x100044000010000c,
    0x2000100008089004, 0x8002040004008080, 0x8000080004008200,
    0x1000100002082001, 0x4000802080040008, 0x8094000202010002,
    0x3010100811000,    0x8204810570a001,   0x100080008001800,
    0x202000400801,     0x44000402001,      0x46000c01001000,
    0x802020001001,     0x2000400282001,    0x5046000402000,
    0x5000029084001,    0x1004040002000,    0x2002010100a00,
    0x1010100008800,    0x8000100008800,    0x4000008002000,
    0x421000200200,     0x25000c0010a00,    0x2000204000800,
    0x12200202010004,   0xa100040008080,    0x1024800010008080,
    0x1010001004008,    0x500020008008080,  0x5000041002008080,
    0x100202010008080,  0x40002000411001,   0x4001002100411,
    0x541002000100391,  0x100002041008003,  0x6c0008224010a00,
    0x504900020008c071, 0x100080a001014001, 0x8000090140100201,
    0x200040444400080};

uint64_t BISHOP_MAGICS[64] = {
    0x89a1121896040024,    0x2004844802002010,    0x2068080051921000,
    0x62880a0220200312,    0x30691a00f040e0,      0x1813da320059b8a0,
    0xc8ca0a500110a4c0,    0x1001d20408b82001,    0x407b7a7f81012000,
    0x1600781804200c00,    0x221a0a124b022400,    0x120308200108022,
    0x8108405181011000,    0x1020c088001000,      0x210c24084091a000,
    0x430800a02000100,     0x14208050a42400,      0x4a1020008ad80001,
    0x42a8810286000400,    0x595602006d1823de,    0x4104410041002200,
    0x21220a080a004200,    0x20204218820090,      0x8102c0408020a00,
    0x5862020202000400,    0x1002020122048000,    0x1105000208001000,
    0x10a6080a01004080,    0x810080a0800a0200,    0x4c080a033001a200,
    0x408904200802000,     0x1008080181001000,    0x28a0084202018001,
    0x1084202402000100,    0x200a0100a080404,     0x802040211028000,
    0x9020840400210000,    0x810000822000400,     0x1000822000a00400,
    0x2200004202040800,    0x4b0c00280040101,     0x4000202401015808,
    0x200420208200,        0x8040020080080080,    0x1010101010100800,
    0x10001000100a008,     0x420821001100,        0x400000200a001000,
    0x2000108904008080,    0x2010200c200c04,      0x11010000600a0000,
    0x280828001000c080,    0x200a418604100080,    0x8184810100080100,
    0x1002488420101008ULL, 0x1004040020800080ULL, 0x802001008080800ULL,
    0x40302010401010,      0x120200402008800,     0x4001090208c0804,
    0x1030200010000080,    0x8020200010008080,    0x1000100200100080,
    0x2000100200801};

Bitboard *ROOK_ATTACKS[64];
Bitboard *BISHOP_ATTACKS[64];

int ROOK_SHIFTS[64];
int BISHOP_SHIFTS[64];

Bitboard ROOK_MASKS[64];
Bitboard BISHOP_MASKS[64];

Bitboard knight_attacks[64];
Bitboard king_attacks[64];

static Bitboard rook_mask(int sq) {
  Bitboard mask = 0;
  int rank = SQ_RANK(sq);
  int file = SQ_FILE(sq);

  // Vertical
  for (int r = rank + 1; r < 7; r++) {
    mask |= 1ULL << SQ(file, r);
  }
  for (int r = rank - 1; r > 0; r--) {
    mask |= 1ULL << SQ(file, r);
  }

  // Horizontal
  for (int f = file + 1; f < 7; f++) {
    mask |= 1ULL << SQ(f, rank);
  }
  for (int f = file - 1; f > 0; f--) {
    mask |= 1ULL << SQ(f, rank);
  }

  return mask;
}

static Bitboard bishop_mask(int sq) {
  Bitboard mask = 0;
  int rank = SQ_RANK(sq);
  int file = SQ_FILE(sq);

  for (int r = rank + 1, f = file + 1; r < 7 && f < 7; r++, f++) {
    mask |= 1ULL << SQ(f, r);
  }
  for (int r = rank + 1, f = file - 1; r < 7 && f > 0; r++, f--) {
    mask |= 1ULL << SQ(f, r);
  }
  for (int r = rank - 1, f = file + 1; r > 0 && f < 7; r--, f++) {
    mask |= 1ULL << SQ(f, r);
  }
  for (int r = rank - 1, f = file - 1; r > 0 && f > 0; r--, f--) {
    mask |= 1ULL << SQ(f, r);
  }

  return mask;
}

static Bitboard rook_attacks_bb(int sq, Bitboard occupied) {
  Bitboard attacks = 0;
  int rank = SQ_RANK(sq);
  int file = SQ_FILE(sq);

  // Right
  for (int f = file + 1; f < 8; f++) {
    attacks |= 1ULL << SQ(f, rank);
    if (occupied & (1ULL << SQ(f, rank)))
      break;
  }
  // Left
  for (int f = file - 1; f >= 0; f--) {
    attacks |= 1ULL << SQ(f, rank);
    if (occupied & (1ULL << SQ(f, rank)))
      break;
  }
  // Up
  for (int r = rank + 1; r < 8; r++) {
    attacks |= 1ULL << SQ(file, r);
    if (occupied & (1ULL << SQ(file, r)))
      break;
  }
  // Down
  for (int r = rank - 1; r >= 0; r--) {
    attacks |= 1ULL << SQ(file, r);
    if (occupied & (1ULL << SQ(file, r)))
      break;
  }

  return attacks;
}

static Bitboard bishop_attacks_bb(int sq, Bitboard occupied) {
  Bitboard attacks = 0;
  int rank = SQ_RANK(sq);
  int file = SQ_FILE(sq);

  // NE
  for (int r = rank + 1, f = file + 1; r < 8 && f < 8; r++, f++) {
    attacks |= 1ULL << SQ(f, r);
    if (occupied & (1ULL << SQ(f, r)))
      break;
  }
  // NW
  for (int r = rank + 1, f = file - 1; r < 8 && f >= 0; r++, f--) {
    attacks |= 1ULL << SQ(f, r);
    if (occupied & (1ULL << SQ(f, r)))
      break;
  }
  // SE
  for (int r = rank - 1, f = file + 1; r >= 0 && f < 8; r--, f++) {
    attacks |= 1ULL << SQ(f, r);
    if (occupied & (1ULL << SQ(f, r)))
      break;
  }
  // SW
  for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; r--, f--) {
    attacks |= 1ULL << SQ(f, r);
    if (occupied & (1ULL << SQ(f, r)))
      break;
  }
  return attacks;
}

static void init_basic_attacks() {
  for (int sq = 0; sq < 64; sq++) {
    Bitboard k_attacks = 0;
    Bitboard kn_attacks = 0;
    int rank = SQ_RANK(sq);
    int file = SQ_FILE(sq);

    // Knight
    int kn_deltas[] = {-17, -15, -10, -6, 6, 10, 15, 17};
    for (int i = 0; i < 8; i++) {
      int to_sq = sq + kn_deltas[i];
      if (to_sq >= 0 && to_sq < 64 && abs(SQ_RANK(to_sq) - rank) <= 2 &&
          abs(SQ_FILE(to_sq) - file) <= 2) {
        kn_attacks |= 1ULL << to_sq;
      }
    }
    knight_attacks[sq] = kn_attacks;

    // King
    int k_deltas[] = {-9, -8, -7, -1, 1, 7, 8, 9};
    for (int i = 0; i < 8; i++) {
      int to_sq = sq + k_deltas[i];
      if (to_sq >= 0 && to_sq < 64 && abs(SQ_RANK(to_sq) - rank) <= 1 &&
          abs(SQ_FILE(to_sq) - file) <= 1) {
        k_attacks |= 1ULL << to_sq;
      }
    }
    king_attacks[sq] = k_attacks;
  }
}

void init_magic_tables() {
  init_basic_attacks();
  for (int sq = 0; sq < 64; sq++) {
    // Rook tables
    ROOK_MASKS[sq] = rook_mask(sq);
    ROOK_SHIFTS[sq] = 64 - POPCOUNT(ROOK_MASKS[sq]);

    int rook_size = 1 << POPCOUNT(ROOK_MASKS[sq]);
    ROOK_ATTACKS[sq] = (Bitboard *)malloc(rook_size * sizeof(Bitboard));

    Bitboard occupied = 0;
    do {
      int index = (occupied * ROOK_MAGICS[sq]) >> ROOK_SHIFTS[sq];
      ROOK_ATTACKS[sq][index] = rook_attacks_bb(sq, occupied);
      occupied = (occupied - ROOK_MASKS[sq]) & ROOK_MASKS[sq];
    } while (occupied);

    // Bishop tables
    BISHOP_MASKS[sq] = bishop_mask(sq);
    BISHOP_SHIFTS[sq] = 64 - POPCOUNT(BISHOP_MASKS[sq]);

    int bishop_size = 1 << POPCOUNT(BISHOP_MASKS[sq]);
    BISHOP_ATTACKS[sq] = (Bitboard *)malloc(bishop_size * sizeof(Bitboard));

    occupied = 0;
    do {
      int index = (occupied * BISHOP_MAGICS[sq]) >> BISHOP_SHIFTS[sq];
      BISHOP_ATTACKS[sq][index] = bishop_attacks_bb(sq, occupied);
      occupied = (occupied - BISHOP_MASKS[sq]) & BISHOP_MASKS[sq];
    } while (occupied);
  }
}

void deinit_magic_tables() {
  for (int sq = 0; sq < 64; sq++) {
    free(ROOK_ATTACKS[sq]);
    free(BISHOP_ATTACKS[sq]);
  }
}
