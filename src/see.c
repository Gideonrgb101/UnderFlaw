#include "see.h"
#include "bitboard.h"
#include "magic.h"
#include "movegen.h"
#include "position.h"
#include "types.h"

static const int see_values[] = {100, 320, 330, 500, 900, 20000, 0};

static Bitboard get_attackers(const Position *pos, int sq, Bitboard occupied) {
  Bitboard attackers = 0;

  // Pawns
  // White pawns at sq-7 (not file A) or sq-9 (not file H) attack sq
  if (sq > 7) {
    if (sq % 8 != 0 && (pos->pieces[WHITE][PAWN] & (1ULL << (sq - 9))))
      attackers |= (1ULL << (sq - 9));
    if (sq % 8 != 7 && (pos->pieces[WHITE][PAWN] & (1ULL << (sq - 7))))
      attackers |= (1ULL << (sq - 7));
  }
  // Black pawns at sq+7 (not file H) or sq+9 (not file A) attack sq
  if (sq < 56) {
    if (sq % 8 != 7 && (pos->pieces[BLACK][PAWN] & (1ULL << (sq + 9))))
      attackers |= (1ULL << (sq + 9));
    if (sq % 8 != 0 && (pos->pieces[BLACK][PAWN] & (1ULL << (sq + 7))))
      attackers |= (1ULL << (sq + 7));
  }

  // Knights
  attackers |= knight_attacks[sq] &
               (pos->pieces[WHITE][KNIGHT] | pos->pieces[BLACK][KNIGHT]);

  // King
  attackers |=
      king_attacks[sq] & (pos->pieces[WHITE][KING] | pos->pieces[BLACK][KING]);

  // Sliding pieces
  attackers |= get_bishop_attacks(sq, occupied) &
               (pos->pieces[WHITE][BISHOP] | pos->pieces[BLACK][BISHOP] |
                pos->pieces[WHITE][QUEEN] | pos->pieces[BLACK][QUEEN]);
  attackers |= get_rook_attacks(sq, occupied) &
               (pos->pieces[WHITE][ROOK] | pos->pieces[BLACK][ROOK] |
                pos->pieces[WHITE][QUEEN] | pos->pieces[BLACK][QUEEN]);

  return attackers;
}

Score see(const Position *pos, Move move) {
  int from = MOVE_FROM(move);
  int to = MOVE_TO(move);

  int captured = position_piece_at(pos, to);
  if (captured == -1) {
    if (MOVE_IS_SPECIAL(move))
      captured = PAWN; // En passant
    else
      return 0; // Not a capture
  }

  int attacker = position_piece_at(pos, from);
  if (attacker == -1)
    return 0;

  Score gain[32];
  gain[0] = see_values[captured];
  int d = 0;

  Bitboard occupied = pos->all;
  Bitboard attackers = get_attackers(pos, to, occupied);
  Bitboard removed = (1ULL << from);
  occupied &= ~removed;

  int color = pos->to_move ^ 1;
  int next_piece = attacker;

  while (d < 31) {
    d++;
    gain[d] = see_values[next_piece] - gain[d - 1];

    // Find smallest attacker for current color
    int found_sq = -1;
    for (int p = PAWN; p <= KING; p++) {
      Bitboard b = attackers & occupied & pos->pieces[color][p];
      if (b) {
        found_sq = LSB(b);
        next_piece = p;
        break;
      }
    }

    if (found_sq == -1) {
      d--;
      break;
    }

    removed |= (1ULL << found_sq);
    occupied &= ~(1ULL << found_sq);

    // Update sliding attackers (X-rays)
    if (next_piece == PAWN || next_piece == BISHOP || next_piece == QUEEN) {
      attackers |= get_bishop_attacks(to, occupied) &
                   (pos->pieces[WHITE][BISHOP] | pos->pieces[BLACK][BISHOP] |
                    pos->pieces[WHITE][QUEEN] | pos->pieces[BLACK][QUEEN]);
    }
    if (next_piece == ROOK || next_piece == QUEEN) {
      attackers |= get_rook_attacks(to, occupied) &
                   (pos->pieces[WHITE][ROOK] | pos->pieces[BLACK][ROOK] |
                    pos->pieces[WHITE][QUEEN] | pos->pieces[BLACK][QUEEN]);
    }

    color ^= 1;
  }

  while (--d > 0) {
    gain[d - 1] = -(-gain[d - 1] > gain[d] ? -gain[d - 1] : gain[d]);
  }

  return gain[0];
}

int see_ge(const Position *pos, Move move, Score threshold) {
  return see(pos, move) >= threshold;
}
