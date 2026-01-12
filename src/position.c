#include "position.h"
#include "bitboard.h"
#include "magic.h"
#include "nnue.h"
#include "types.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Zobrist hash tables
uint64_t zobrist_piece[2][6][64];
uint64_t zobrist_castle[16];
uint64_t zobrist_enpassant[8];
uint64_t zobrist_to_move;

static uint64_t xorshift64(uint64_t *state) {
  uint64_t x = *state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  *state = x;
  return x;
}

void zobrist_init() {
  uint64_t state = 0x0123456789ABCDEFULL;

  for (int color = 0; color < 2; color++) {
    for (int piece = 0; piece < 6; piece++) {
      for (int sq = 0; sq < 64; sq++) {
        zobrist_piece[color][piece][sq] = xorshift64(&state);
      }
    }
  }

  for (int i = 0; i < 16; i++) {
    zobrist_castle[i] = xorshift64(&state);
  }

  for (int i = 0; i < 8; i++) {
    zobrist_enpassant[i] = xorshift64(&state);
  }

  zobrist_to_move = xorshift64(&state);
}

void position_init(Position *pos) {
  memset(pos, 0, sizeof(Position));
  pos->enpassant = -1;
}

void position_from_fen(Position *pos, const char *fen) {
  position_init(pos);

  const char *p = fen;
  int rank = 7, file = 0;

  // Board
  while (*p && *p != ' ') {
    if (*p == '/') {
      rank--;
      file = 0;
    } else if (isdigit(*p)) {
      file += *p - '0';
    } else {
      int color = isupper(*p) ? WHITE : BLACK;
      int piece = -1;

      switch (tolower(*p)) {
      case 'p':
        piece = PAWN;
        break;
      case 'n':
        piece = KNIGHT;
        break;
      case 'b':
        piece = BISHOP;
        break;
      case 'r':
        piece = ROOK;
        break;
      case 'q':
        piece = QUEEN;
        break;
      case 'k':
        piece = KING;
        break;
      }

      if (piece >= 0) {
        position_set_piece(pos, piece, color, SQ(file, rank));
        file++;
      }
    }
    p++;
  }

  // Active color
  p++;
  pos->to_move = (*p == 'w') ? WHITE : BLACK;
  p += 2;

  // Castling
  pos->castling = 0;
  // Initialize castling rooks to default values (standard chess)
  pos->castling_rooks[0] = SQ_H1;
  pos->castling_rooks[1] = SQ_A1;
  pos->castling_rooks[2] = SQ_H8;
  pos->castling_rooks[3] = SQ_A8;

  int w_king_sq = -1;
  int b_king_sq = -1;
  Bitboard wk = pos->pieces[WHITE][KING];
  Bitboard bk = pos->pieces[BLACK][KING];
  if (wk)
    w_king_sq = LSB(wk);
  if (bk)
    b_king_sq = LSB(bk);

  while (*p && *p != ' ') {
    int token = *p;
    int index = -1;
    int rook_sq = -1;
    int is_white = 1;

    if (token == 'K') {
      pos->castling |= 1;
      // Find rightmost rook
      Bitboard rooks = pos->pieces[WHITE][ROOK];
      if (rooks) {
        while (rooks) {
          int sq = LSB(rooks);
          rooks &= rooks - 1;
          if (pos->castling_rooks[0] == SQ_H1 ||
              SQ_FILE(sq) > SQ_FILE(pos->castling_rooks[0]))
            pos->castling_rooks[0] = sq;
          // Standard interpretation: K is H-side. Use H-file rook if exists,
          // else rightmost? Actually for 'K', we assume standard H1 unless
          // proven otherwise? Better: If 'K', try H1. If H1 is not rook, find
          // rightmost.
          if (position_piece_at(pos, SQ_H1) == ROOK &&
              (pos->pieces[WHITE][ROOK] & (1ULL << SQ_H1)))
            pos->castling_rooks[0] = SQ_H1;
          else {
            // Fallback logic could be complex. For now assuming H1 for 'K' or
            // finding rightmost. Let's rely on standard case being H1.
          }
        }
      }
    } else if (token == 'Q') {
      pos->castling |= 2;
      pos->castling_rooks[1] = SQ_A1;
    } // Assuming A1 for Q
    else if (token == 'k') {
      pos->castling |= 4;
      pos->castling_rooks[2] = SQ_H8;
    } // Assuming H8 for k
    else if (token == 'q') {
      pos->castling |= 8;
      pos->castling_rooks[3] = SQ_A8;
    } // Assuming A8 for q
    else if (token >= 'A' && token <= 'H') {
      // FRC White Rook
      is_white = 1;
      int file = token - 'A';
      rook_sq = SQ(file, 0); // Rank 1
    } else if (token >= 'a' && token <= 'h') {
      // FRC Black Rook
      is_white = 0;
      int file = token - 'a';
      rook_sq = SQ(file, 7); // Rank 8
    }

    // Logic for letter-based castling rights
    if (rook_sq != -1) {
      // Check if white or black
      if (is_white) {
        // Determine if King-side or Queen-side based on King position
        if (w_king_sq != -1) {
          if (SQ_FILE(rook_sq) > SQ_FILE(w_king_sq)) {
            // Right of King -> King-side
            pos->castling |= 1;
            pos->castling_rooks[0] = rook_sq;
          } else {
            // Left of King -> Queen-side
            pos->castling |= 2;
            pos->castling_rooks[1] = rook_sq;
          }
        }
      } else {
        if (b_king_sq != -1) {
          if (SQ_FILE(rook_sq) > SQ_FILE(b_king_sq)) {
            pos->castling |= 4;
            pos->castling_rooks[2] = rook_sq;
          } else {
            pos->castling |= 8;
            pos->castling_rooks[3] = rook_sq;
          }
        }
      }
    }
    p++;
  }
  p++;

  // En passant
  if (*p != '-') {
    pos->enpassant = SQ(p[0] - 'a', p[1] - '1');
    p += 2;
  } else {
    pos->enpassant = -1;
    p++;
  }
  p++;

  // Halfmove
  pos->halfmove = 0;
  while (*p && *p != ' ') {
    pos->halfmove = pos->halfmove * 10 + (*p - '0');
    p++;
  }
  p++;

  // Fullmove
  pos->fullmove = 1;
  while (*p) {
    pos->fullmove = pos->fullmove * 10 + (*p - '0');
    p++;
  }

  // Recalculate derived data
  pos->occupied[WHITE] = 0;
  pos->occupied[BLACK] = 0;
  for (int piece = 0; piece < 6; piece++) {
    pos->occupied[WHITE] |= pos->pieces[WHITE][piece];
    pos->occupied[BLACK] |= pos->pieces[BLACK][piece];
  }
  pos->all = pos->occupied[WHITE] | pos->occupied[BLACK];

  pos->zobrist = position_hash(pos);
  pos->pawn_hash = position_pawn_hash(pos);

  // Initialize NNUE accumulator
  if (nnue_available) {
    nnue_refresh_accumulator(pos);
  }
}

void position_to_fen(const Position *pos, char *fen, int max_len) {
  int idx = 0;

  for (int rank = 7; rank >= 0; rank--) {
    int empty = 0;
    for (int file = 0; file < 8; file++) {
      int sq = SQ(file, rank);
      int piece = position_piece_at(pos, sq);

      if (piece < 0) {
        empty++;
      } else {
        if (empty > 0) {
          if (idx >= max_len - 1)
            return;
          fen[idx++] = '0' + empty;
          empty = 0;
        }

        const char *pieces = "pnbrqkPNBRQK";
        for (int i = 0; i < 6; i++) {
          if (pos->pieces[WHITE][i] & (1ULL << sq)) {
            if (idx >= max_len - 1)
              return;
            fen[idx++] = pieces[i + 6];
            goto next_sq;
          }
        }
        for (int i = 0; i < 6; i++) {
          if (pos->pieces[BLACK][i] & (1ULL << sq)) {
            if (idx >= max_len - 1)
              return;
            fen[idx++] = pieces[i];
            goto next_sq;
          }
        }
      next_sq:;
      }
    }

    if (empty > 0) {
      if (idx >= max_len - 1)
        return;
      fen[idx++] = '0' + empty;
    }

    if (rank > 0) {
      if (idx >= max_len - 1)
        return;
      fen[idx++] = '/';
    }
  }

  if (idx >= max_len - 1)
    return;
  fen[idx++] = ' ';
  fen[idx++] = pos->to_move == WHITE ? 'w' : 'b';
  fen[idx++] = ' ';

  if (pos->castling == 0) {
    fen[idx++] = '-';
  } else {
    if (pos->castling & 1)
      fen[idx++] = 'K';
    if (pos->castling & 2)
      fen[idx++] = 'Q';
    if (pos->castling & 4)
      fen[idx++] = 'k';
    if (pos->castling & 8)
      fen[idx++] = 'q';
  }

  fen[idx++] = ' ';

  if (pos->enpassant < 0) {
    fen[idx++] = '-';
  } else {
    fen[idx++] = 'a' + SQ_FILE(pos->enpassant);
    fen[idx++] = '1' + SQ_RANK(pos->enpassant);
  }

  fen[idx++] = ' ';
  idx +=
      snprintf(fen + idx, max_len - idx, "%d %d", pos->halfmove, pos->fullmove);

  if (idx < max_len) {
    fen[idx] = '\0';
  }
}

int position_piece_at(const Position *pos, int sq) {
  for (int piece = 0; piece < 6; piece++) {
    if (pos->pieces[WHITE][piece] & (1ULL << sq))
      return piece;
    if (pos->pieces[BLACK][piece] & (1ULL << sq))
      return piece + 6;
  }
  return -1;
}

#include "nnue.h"

void position_set_piece(Position *pos, int piece, int color, int sq) {
  pos->pieces[color][piece] |= 1ULL << sq;
  pos->occupied[color] |= 1ULL << sq;
  pos->all |= 1ULL << sq;
}

void position_remove_piece(Position *pos, int piece, int color, int sq) {
  pos->pieces[color][piece] &= ~(1ULL << sq);
  pos->occupied[color] &= ~(1ULL << sq);
  pos->all &= ~(1ULL << sq);
}

void position_move_piece(Position *pos, int piece, int color, int from,
                         int to) {
  uint64_t from_to = (1ULL << from) | (1ULL << to);
  pos->pieces[color][piece] ^= from_to;
  pos->occupied[color] ^= from_to;
  pos->all ^= from_to;
}

int position_in_check(const Position *pos) {
  int color = pos->to_move;
  Bitboard king = pos->pieces[color][KING];
  if (king == 0)
    return 0;

  int king_sq = LSB(king);
  int enemy = color ^ 1;

  // 1. Check pawn attacks
  Bitboard enemy_pawns = pos->pieces[enemy][PAWN];
  if (color == WHITE) {
    if (SQ_RANK(king_sq) < 7) {
      if (SQ_FILE(king_sq) > 0 && (enemy_pawns & (1ULL << (king_sq + 7))))
        return 1;
      if (SQ_FILE(king_sq) < 7 && (enemy_pawns & (1ULL << (king_sq + 9))))
        return 1;
    }
  } else {
    if (SQ_RANK(king_sq) > 0) {
      if (SQ_FILE(king_sq) > 0 && (enemy_pawns & (1ULL << (king_sq - 9))))
        return 1;
      if (SQ_FILE(king_sq) < 7 && (enemy_pawns & (1ULL << (king_sq - 7))))
        return 1;
    }
  }

  // 2. Check knight attacks
  if (knight_attacks[king_sq] & pos->pieces[enemy][KNIGHT])
    return 1;

  // 3. Check slider attacks
  Bitboard occupied = pos->all;
  if (get_rook_attacks(king_sq, occupied) &
      (pos->pieces[enemy][ROOK] | pos->pieces[enemy][QUEEN]))
    return 1;
  if (get_bishop_attacks(king_sq, occupied) &
      (pos->pieces[enemy][BISHOP] | pos->pieces[enemy][QUEEN]))
    return 1;

  // 4. Check king attacks
  if (king_attacks[king_sq] & pos->pieces[enemy][KING])
    return 1;

  return 0;
}

int position_piece_count(const Position *pos, int piece, int color) {
  return POPCOUNT(pos->pieces[color][piece]);
}

int position_material_count(const Position *pos, int color) {
  int total = 0;
  for (int piece = 0; piece < 5; piece++) {
    total += POPCOUNT(pos->pieces[color][piece]) * piece_value(piece);
  }
  return total;
}

uint64_t position_hash(const Position *pos) {
  uint64_t hash = 0;

  for (int color = 0; color < 2; color++) {
    for (int piece = 0; piece < 6; piece++) {
      Bitboard bb = pos->pieces[color][piece];
      int sq;
      BB_FOREACH(sq, bb) { hash ^= zobrist_piece[color][piece][sq]; }
    }
  }

  hash ^= zobrist_castle[pos->castling];

  if (pos->enpassant >= 0) {
    hash ^= zobrist_enpassant[SQ_FILE(pos->enpassant)];
  }

  if (pos->to_move == BLACK) {
    hash ^= zobrist_to_move;
  }

  return hash;
}

uint64_t position_pawn_hash(const Position *pos) {
  uint64_t hash = 0;

  for (int color = 0; color < 2; color++) {
    Bitboard bb = pos->pieces[color][PAWN];
    int sq;
    BB_FOREACH(sq, bb) { hash ^= zobrist_piece[color][PAWN][sq]; }
  }

  return hash;
}

void position_make_move(Position *pos, Move move, UndoInfo *undo) {
  undo->move = move;
  undo->captured = -1;
  undo->castling = pos->castling;
  undo->enpassant = pos->enpassant;
  undo->halfmove = pos->halfmove;
  undo->zobrist = pos->zobrist;
  undo->pawn_hash = pos->pawn_hash;
  undo->accumulator = pos->accumulator;

  int from = MOVE_FROM(move);
  int to = MOVE_TO(move);
  int promo = MOVE_PROMO(move);
  int color = pos->to_move;
  int enemy = color ^ 1;

  // Find moving piece
  int moving_piece = -1;
  for (int piece = 0; piece < 6; piece++) {
    if (pos->pieces[color][piece] & (1ULL << from)) {
      moving_piece = piece;
      break;
    }
  }
  if (moving_piece < 0)
    return;
  undo->moving_piece = moving_piece;

  // Handle captures
  for (int piece = 0; piece < 6; piece++) {
    if (pos->pieces[enemy][piece] & (1ULL << to)) {
      undo->captured = piece;
      position_remove_piece(pos, piece, enemy, to);
      pos->zobrist ^= zobrist_piece[enemy][piece][to];
      if (piece == PAWN)
        pos->pawn_hash ^= zobrist_piece[enemy][piece][to];

      // Update castling rights if a rook is captured
      if (to == pos->castling_rooks[0])
        pos->castling &= ~1;
      else if (to == pos->castling_rooks[1])
        pos->castling &= ~2;
      else if (to == pos->castling_rooks[2])
        pos->castling &= ~4;
      else if (to == pos->castling_rooks[3])
        pos->castling &= ~8;
      break;
    }
  }

  // Handle en passant capture
  if (moving_piece == PAWN && to == pos->enpassant) {
    int captured_sq = SQ(SQ_FILE(to), SQ_RANK(from));
    position_remove_piece(pos, PAWN, enemy, captured_sq);
    pos->zobrist ^= zobrist_piece[enemy][PAWN][captured_sq];
    pos->pawn_hash ^= zobrist_piece[enemy][PAWN][captured_sq];
    undo->captured = PAWN;
  }

  // Update castling rights for King or Rook move
  if (moving_piece == KING) {
    if (color == WHITE)
      pos->castling &= ~3;
    else
      pos->castling &= ~12;
  } else if (moving_piece == ROOK) {
    if (color == WHITE) {
      if (from == pos->castling_rooks[0])
        pos->castling &= ~1;
      else if (from == pos->castling_rooks[1])
        pos->castling &= ~2;
    } else {
      if (from == pos->castling_rooks[2])
        pos->castling &= ~4;
      else if (from == pos->castling_rooks[3])
        pos->castling &= ~8;
    }
  }

  // Actually move the king/piece
  if (moving_piece == KING && MOVE_IS_SPECIAL(move)) {
    int is_kingside = (to == pos->castling_rooks[color == WHITE ? 0 : 2]);
    int king_to = (color == WHITE) ? (is_kingside ? SQ_G1 : SQ_C1)
                                   : (is_kingside ? SQ_G8 : SQ_C8);
    int rook_to = (color == WHITE) ? (is_kingside ? SQ_F1 : SQ_D1)
                                   : (is_kingside ? SQ_F8 : SQ_D8);
    int rook_sq = to;

    position_move_piece(pos, KING, color, from, king_to);
    pos->zobrist ^= zobrist_piece[color][KING][from];
    pos->zobrist ^= zobrist_piece[color][KING][king_to];

    position_move_piece(pos, ROOK, color, rook_sq, rook_to);
    pos->zobrist ^= zobrist_piece[color][ROOK][rook_sq];
    pos->zobrist ^= zobrist_piece[color][ROOK][rook_to];
  } else {
    position_move_piece(pos, moving_piece, color, from, to);
    pos->zobrist ^= zobrist_piece[color][moving_piece][from];
    pos->zobrist ^= zobrist_piece[color][moving_piece][to];
    if (moving_piece == PAWN) {
      pos->pawn_hash ^= zobrist_piece[color][PAWN][from];
      pos->pawn_hash ^= zobrist_piece[color][PAWN][to];
    }
  }

  // Promotions
  if (promo > 0) {
    position_remove_piece(pos, PAWN, color, to);
    position_set_piece(pos, promo, color, to);
    pos->zobrist ^= zobrist_piece[color][PAWN][to];
    pos->zobrist ^= zobrist_piece[color][promo][to];
    pos->pawn_hash ^= zobrist_piece[color][PAWN][to];
  }

  // Update en passant state
  if (pos->enpassant >= 0)
    pos->zobrist ^= zobrist_enpassant[SQ_FILE(pos->enpassant)];
  pos->enpassant = -1;
  if (moving_piece == PAWN && abs(to - from) == 16) {
    pos->enpassant = (from + to) / 2;
    pos->zobrist ^= zobrist_enpassant[SQ_FILE(pos->enpassant)];
  }

  // Counters
  if (moving_piece == PAWN || undo->captured >= 0)
    pos->halfmove = 0;
  else
    pos->halfmove++;
  if (color == BLACK)
    pos->fullmove++;

  // Final zobrist updates
  pos->zobrist ^= zobrist_castle[undo->castling];
  pos->zobrist ^= zobrist_castle[pos->castling];

  // ===== NNUE Accumulator Update =====
  if (nnue_available) {
    if (moving_piece == KING || promo > 0 || undo->enpassant >= 0 ||
        MOVE_IS_SPECIAL(move)) {
      // King moves or complex moves: full refresh is safest for now
      // (Optimized incremental updates for these can be added later)
      nnue_refresh_accumulator(pos);
    } else {
      int w_king = LSB(pos->pieces[WHITE][KING]);
      int b_king = LSB(pos->pieces[BLACK][KING]);

      // Remove piece from old square
      nnue_update_accumulator(&pos->accumulator, moving_piece, color, from, 0,
                              w_king, b_king);
      // Add piece to new square
      nnue_update_accumulator(&pos->accumulator, moving_piece, color, to, 1,
                              w_king, b_king);

      // Handle capture
      if (undo->captured >= 0) {
        nnue_update_accumulator(&pos->accumulator, undo->captured, enemy, to, 0,
                                w_king, b_king);
      }
    }
  }
  pos->to_move = enemy;
  pos->zobrist ^= zobrist_to_move;
}

void position_unmake_move(Position *pos, Move move, const UndoInfo *undo) {
  int color = pos->to_move ^ 1;
  int from = MOVE_FROM(move);
  int to = MOVE_TO(move);
  int promo = MOVE_PROMO(move);
  int moving_piece = undo->moving_piece;

  if (moving_piece == KING && MOVE_IS_SPECIAL(move)) {
    int is_kingside = (to == pos->castling_rooks[color == WHITE ? 0 : 2]);
    int king_to = (color == WHITE) ? (is_kingside ? SQ_G1 : SQ_C1)
                                   : (is_kingside ? SQ_G8 : SQ_C8);
    int rook_to = (color == WHITE) ? (is_kingside ? SQ_F1 : SQ_D1)
                                   : (is_kingside ? SQ_F8 : SQ_D8);
    int rook_sq = to;

    position_move_piece(pos, ROOK, color, rook_to, rook_sq);
    position_move_piece(pos, KING, color, king_to, from);
  } else {
    if (promo > 0) {
      position_remove_piece(pos, promo, color, to);
      position_set_piece(pos, PAWN, color, from);
    } else {
      position_move_piece(pos, moving_piece, color, to, from);
    }
  }

  if (undo->captured >= 0) {
    if (moving_piece == PAWN && to == undo->enpassant) {
      int captured_sq = SQ(SQ_FILE(to), SQ_RANK(from));
      position_set_piece(pos, PAWN, color ^ 1, captured_sq);
    } else {
      position_set_piece(pos, undo->captured, color ^ 1, to);
    }
  }

  pos->to_move = color;
  pos->castling = undo->castling;
  pos->enpassant = undo->enpassant;
  pos->halfmove = undo->halfmove;
  pos->zobrist = undo->zobrist;
  pos->pawn_hash = undo->pawn_hash;
  pos->accumulator = undo->accumulator;

  // Sync occupied bitboards (since we used helpers, they should be mostly OK,
  // but for safety)
  pos->occupied[WHITE] = 0;
  pos->occupied[BLACK] = 0;
  for (int p = 0; p < 6; p++) {
    pos->occupied[WHITE] |= pos->pieces[WHITE][p];
    pos->occupied[BLACK] |= pos->pieces[BLACK][p];
  }
  pos->all = pos->occupied[WHITE] | pos->occupied[BLACK];
}
