#ifndef TYPES_H
#define TYPES_H

#include <stdalign.h>
#include <stdint.h>

// Pieces
#define PAWN 0
#define KNIGHT 1
#define BISHOP 2
#define ROOK 3
#define QUEEN 4
#define KING 5

// Colors
#define WHITE 0
#define BLACK 1

// Squares
enum {
  SQ_A1,
  SQ_B1,
  SQ_C1,
  SQ_D1,
  SQ_E1,
  SQ_F1,
  SQ_G1,
  SQ_H1,
  SQ_A2,
  SQ_B2,
  SQ_C2,
  SQ_D2,
  SQ_E2,
  SQ_F2,
  SQ_G2,
  SQ_H2,
  SQ_A3,
  SQ_B3,
  SQ_C3,
  SQ_D3,
  SQ_E3,
  SQ_F3,
  SQ_G3,
  SQ_H3,
  SQ_A4,
  SQ_B4,
  SQ_C4,
  SQ_D4,
  SQ_E4,
  SQ_F4,
  SQ_G4,
  SQ_H4,
  SQ_A5,
  SQ_B5,
  SQ_C5,
  SQ_D5,
  SQ_E5,
  SQ_F5,
  SQ_G5,
  SQ_H5,
  SQ_A6,
  SQ_B6,
  SQ_C6,
  SQ_D6,
  SQ_E6,
  SQ_F6,
  SQ_G6,
  SQ_H6,
  SQ_A7,
  SQ_B7,
  SQ_C7,
  SQ_D7,
  SQ_E7,
  SQ_F7,
  SQ_G7,
  SQ_H7,
  SQ_A8,
  SQ_B8,
  SQ_C8,
  SQ_D8,
  SQ_E8,
  SQ_F8,
  SQ_G8,
  SQ_H8,
  SQ_NONE = 64
};

// NNUE Architecture Constants (Half-KP: 64 King Squares * 10 Piece types * 64
// Squares)
#define NNUE_INPUT_DIM 40960
#define NNUE_HIDDEN_DIM 256
#define NNUE_FACTOR 16

typedef struct {
  alignas(32) int16_t white[NNUE_HIDDEN_DIM];
  alignas(32) int16_t black[NNUE_HIDDEN_DIM];
  int computed_accumulation;
} Accumulator;

// Move encoding
typedef uint32_t Move;

#define MOVE_NONE 0

#define MOVE_FROM(m) ((m) & 0x3F)
#define MOVE_TO(m) (((m) >> 6) & 0x3F)
#define MOVE_PROMO(m) (((m) >> 12) & 0xF)
#define MOVE_FLAG(m) (((m) >> 16) & 0x3)

#define FLAG_QUIET 0
#define FLAG_CAPTURE 1
#define FLAG_SPECIAL 2

#define MAKE_MOVE(from, to, promo, flag)                                       \
  ((from) | ((to) << 6) | ((promo) << 12) | ((flag) << 16))

#define MOVE_IS_CAPTURE(m) (MOVE_FLAG(m) == FLAG_CAPTURE)
#define MOVE_IS_PROMO(m) (MOVE_PROMO(m) != 0)
#define MOVE_IS_SPECIAL(m) (MOVE_FLAG(m) == FLAG_SPECIAL)

// Square scores
typedef int16_t Score;
#define SCORE_INFINITE 32000
#define SCORE_MATE 31000
#define SCORE_TB_WIN 30000

// Piece values
#define VALUE_PAWN 100
#define VALUE_KNIGHT 320
#define VALUE_BISHOP 330
#define VALUE_ROOK 500
#define VALUE_QUEEN 900
#define VALUE_KING 0

static inline int piece_value(int piece) {
  const int values[] = {100, 320, 330, 500, 900, 0};
  return values[piece];
}

// Square names for debugging
static inline const char *square_name(int sq) {
  static char buffer[3];
  buffer[0] = 'a' + (sq & 7);
  buffer[1] = '1' + (sq >> 3);
  buffer[2] = '\0';
  return buffer;
}

#endif // TYPES_H
