#include "movegen.h"
#include "bitboard.h"
#include "magic.h"
#include "position.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>

// Knight/King attacks lookup tables are now in magic.c/h

static void add_move(MoveList *list, int from, int to, int flag) {
  if (list->count < 256) {
    Move m = MAKE_MOVE(from, to, 0, flag);
    list->moves[list->count++] = m;
  }
}

static void add_pawn_moves(MoveList *list, const Position *pos, int from,
                           int to, int flag) {
  int rank = SQ_RANK(to);
  int color = pos->to_move;

  if ((color == WHITE && rank == 7) || (color == BLACK && rank == 0)) {
    // Promotion
    for (int promo = KNIGHT; promo <= QUEEN; promo++) {
      if (list->count < 256) {
        list->moves[list->count++] =
            MAKE_MOVE(from, to, promo - KNIGHT + 1, flag);
      }
    }
  } else {
    add_move(list, from, to, flag);
  }
}

void movegen_all(const Position *pos, MoveList *list) {

  list->count = 0;
  int color = pos->to_move;
  int enemy = color ^ 1;
  Bitboard empty = ~pos->all;
  Bitboard enemies = pos->occupied[enemy];

  // Pawns
  Bitboard pawns = pos->pieces[color][PAWN];
  if (color == WHITE) {
    Bitboard push_1 = (pawns << 8) & empty;
    int sq;
    BB_FOREACH(sq, push_1) {
      add_pawn_moves(list, pos, sq - 8, sq, FLAG_QUIET);
    }

    Bitboard push_2 = ((push_1 & RANK_3) << 8) & empty;
    BB_FOREACH(sq, push_2) { add_move(list, sq - 16, sq, FLAG_QUIET); }

    Bitboard capture_left = ((pawns << 7) & ~FILE_H) & enemies;
    BB_FOREACH(sq, capture_left) {
      add_pawn_moves(list, pos, sq - 7, sq, FLAG_CAPTURE);
    }

    Bitboard capture_right = ((pawns << 9) & ~FILE_A) & enemies;
    BB_FOREACH(sq, capture_right) {
      add_pawn_moves(list, pos, sq - 9, sq, FLAG_CAPTURE);
    }

    if (pos->enpassant >= 0) {
      int ep_file = SQ_FILE(pos->enpassant);
      if (ep_file > 0) {
        int pawn_sq = SQ(ep_file - 1, 4);
        if (pawns & (1ULL << pawn_sq)) {
          add_move(list, pawn_sq, pos->enpassant, FLAG_CAPTURE);
        }
      }
      if (ep_file < 7) {
        int pawn_sq = SQ(ep_file + 1, 4);
        if (pawns & (1ULL << pawn_sq)) {
          add_move(list, pawn_sq, pos->enpassant, FLAG_CAPTURE);
        }
      }
    }
  } else {
    Bitboard push_1 = (pawns >> 8) & empty;
    int sq;
    BB_FOREACH(sq, push_1) {
      add_pawn_moves(list, pos, sq + 8, sq, FLAG_QUIET);
    }

    Bitboard push_2 = ((push_1 & RANK_6) >> 8) & empty;
    BB_FOREACH(sq, push_2) { add_move(list, sq + 16, sq, FLAG_QUIET); }

    Bitboard capture_left = ((pawns >> 7) & ~FILE_A) & enemies;
    BB_FOREACH(sq, capture_left) {
      add_pawn_moves(list, pos, sq + 7, sq, FLAG_CAPTURE);
    }

    Bitboard capture_right = ((pawns >> 9) & ~FILE_H) & enemies;
    BB_FOREACH(sq, capture_right) {
      add_pawn_moves(list, pos, sq + 9, sq, FLAG_CAPTURE);
    }

    if (pos->enpassant >= 0) {
      int ep_file = SQ_FILE(pos->enpassant);
      if (ep_file > 0) {
        int pawn_sq = SQ(ep_file - 1, 3);
        if (pawns & (1ULL << pawn_sq)) {
          add_move(list, pawn_sq, pos->enpassant, FLAG_CAPTURE);
        }
      }
      if (ep_file < 7) {
        int pawn_sq = SQ(ep_file + 1, 3);
        if (pawns & (1ULL << pawn_sq)) {
          add_move(list, pawn_sq, pos->enpassant, FLAG_CAPTURE);
        }
      }
    }
  }

  // Knights
  Bitboard knights = pos->pieces[color][KNIGHT];
  int sq;
  BB_FOREACH(sq, knights) {
    Bitboard moves = knight_attacks[sq] & ~pos->occupied[color];
    int to;
    BB_FOREACH(to, moves) {
      add_move(list, sq, to,
               (enemies & (1ULL << to)) ? FLAG_CAPTURE : FLAG_QUIET);
    }
  }

  // Bishops
  Bitboard bishops = pos->pieces[color][BISHOP];
  BB_FOREACH(sq, bishops) {
    Bitboard moves = get_bishop_attacks(sq, pos->all) & ~pos->occupied[color];
    int to;
    BB_FOREACH(to, moves) {
      add_move(list, sq, to,
               (enemies & (1ULL << to)) ? FLAG_CAPTURE : FLAG_QUIET);
    }
  }

  // Rooks
  Bitboard rooks = pos->pieces[color][ROOK];
  BB_FOREACH(sq, rooks) {
    Bitboard moves = get_rook_attacks(sq, pos->all) & ~pos->occupied[color];
    int to;
    BB_FOREACH(to, moves) {
      add_move(list, sq, to,
               (enemies & (1ULL << to)) ? FLAG_CAPTURE : FLAG_QUIET);
    }
  }

  // Queens
  Bitboard queens = pos->pieces[color][QUEEN];
  BB_FOREACH(sq, queens) {
    Bitboard moves = get_queen_attacks(sq, pos->all) & ~pos->occupied[color];
    int to;
    BB_FOREACH(to, moves) {
      add_move(list, sq, to,
               (enemies & (1ULL << to)) ? FLAG_CAPTURE : FLAG_QUIET);
    }
  }

  // King
  Bitboard kings = pos->pieces[color][KING];
  if (kings) {
    sq = LSB(kings);
    Bitboard moves = king_attacks[sq] & ~pos->occupied[color];
    int to;
    BB_FOREACH(to, moves) {
      add_move(list, sq, to,
               (enemies & (1ULL << to)) ? FLAG_CAPTURE : FLAG_QUIET);
    }

    // Castling
    for (int i = 0; i < 2; i++) {
      // i=0: King-side (Bit 1/4), i=1: Queen-side (Bit 2/8)
      int castling_bit = (color == WHITE) ? (1 << i) : (4 << i);
      if (pos->castling & castling_bit) {
        int rook_sq = pos->castling_rooks[(color == WHITE) ? i : (2 + i)];

        // Verify rook is actually there (it should be if bit is set, but
        // safety)
        if (!(pos->pieces[color][ROOK] & (1ULL << rook_sq)))
          continue;

        int king_sq = sq; // Current King Square

        // Target Checks
        int target_k = (color == WHITE) ? ((i == 0) ? SQ_G1 : SQ_C1)
                                        : ((i == 0) ? SQ_G8 : SQ_C8);
        int target_r = (color == WHITE) ? ((i == 0) ? SQ_F1 : SQ_D1)
                                        : ((i == 0) ? SQ_F8 : SQ_D8);

        // Check paths
        // 1. King Path: From KingSq to KingDest
        // 2. Rook Path: From RookSq to RookDest
        // Must be empty (excluding K and R themselves)
        // Note: In FRC, K and R might obstruct each other's path if not handled
        // carefully, but standard FRC logic says "squares between start and end
        // must be vacant".

        int blocked = 0;

        // Check King travel path
        int k_step = (target_k > king_sq) ? 1 : -1;
        for (int s = king_sq + k_step;; s += k_step) {
          if (s != king_sq && s != rook_sq) {
            if (pos->all & (1ULL << s)) {
              blocked = 1;
              break;
            }
          }
          if (s == target_k)
            break;
        }
        if (blocked)
          continue;

        // Check Rook travel path
        int r_step = (target_r > rook_sq) ? 1 : -1;
        for (int s = rook_sq + r_step;; s += r_step) {
          if (s != king_sq && s != rook_sq) {
            if (pos->all & (1ULL << s)) {
              blocked = 1;
              break;
            }
          }
          if (s == target_r)
            break;
        }
        if (blocked)
          continue;

        // Check if King passes through check or ends up in check
        // We must verify:
        // 1. King not in check at start (already checked by caller usually? No,
        // caller generates pseudolegal?)
        //    Actually, UCI says "King cannot castle if in check".
        //    movegen_is_legal usually checks the *result*.
        if (position_in_check(pos))
          continue; // Can't castle out of check

        // Check Path (King only)
        // Rules say King cannot pass through attacked square.
        int enemy = color ^ 1;
        int path_blocked_by_check = 0;
        for (int s = king_sq;; s += k_step) {
          // Inline attack check for square 's'
          if (knight_attacks[s] & pos->pieces[enemy][KNIGHT]) {
            path_blocked_by_check = 1;
            break;
          }
          if (king_attacks[s] & pos->pieces[enemy][KING]) {
            path_blocked_by_check = 1;
            break;
          }

          Bitboard occupied = pos->all;
          if (get_bishop_attacks(s, occupied) &
              (pos->pieces[enemy][BISHOP] | pos->pieces[enemy][QUEEN])) {
            path_blocked_by_check = 1;
            break;
          }
          if (get_rook_attacks(s, occupied) &
              (pos->pieces[enemy][ROOK] | pos->pieces[enemy][QUEEN])) {
            path_blocked_by_check = 1;
            break;
          }

          Bitboard pawns = pos->pieces[enemy][PAWN];
          Bitboard p_mask = (enemy == WHITE)
                                ? (((1ULL << s) >> 7) & ~FILE_A) |
                                      (((1ULL << s) >> 9) & ~FILE_H)
                                : (((1ULL << s) << 7) & ~FILE_H) |
                                      (((1ULL << s) << 9) & ~FILE_A);
          if (p_mask & pawns) {
            path_blocked_by_check = 1;
            break;
          }

          if (s == target_k)
            break;
        }

        if (path_blocked_by_check) {
          continue;
        }

        add_move(list, king_sq, rook_sq, FLAG_SPECIAL);
      }
    }
  }
}

void movegen_captures(const Position *pos, MoveList *list) {
  list->count = 0;
  int color = pos->to_move;
  Bitboard enemies = pos->occupied[color ^ 1];

  // Pawns
  Bitboard pawns = pos->pieces[color][PAWN];
  if (color == WHITE) {
    Bitboard capture_left = ((pawns << 7) & ~FILE_H) & enemies;
    int sq;
    BB_FOREACH(sq, capture_left) {
      add_pawn_moves(list, pos, sq - 7, sq, FLAG_CAPTURE);
    }

    Bitboard capture_right = ((pawns << 9) & ~FILE_A) & enemies;
    BB_FOREACH(sq, capture_right) {
      add_pawn_moves(list, pos, sq - 9, sq, FLAG_CAPTURE);
    }

    if (pos->enpassant >= 0) {
      int ep_file = SQ_FILE(pos->enpassant);
      if (ep_file > 0) {
        int pawn_sq = SQ(ep_file - 1, 4);
        if (pawns & (1ULL << pawn_sq)) {
          add_move(list, pawn_sq, pos->enpassant, FLAG_CAPTURE);
        }
      }
      if (ep_file < 7) {
        int pawn_sq = SQ(ep_file + 1, 4);
        if (pawns & (1ULL << pawn_sq)) {
          add_move(list, pawn_sq, pos->enpassant, FLAG_CAPTURE);
        }
      }
    }
  } else {
    Bitboard capture_left = ((pawns >> 7) & ~FILE_A) & enemies;
    int sq;
    BB_FOREACH(sq, capture_left) {
      add_pawn_moves(list, pos, sq + 7, sq, FLAG_CAPTURE);
    }

    Bitboard capture_right = ((pawns >> 9) & ~FILE_H) & enemies;
    BB_FOREACH(sq, capture_right) {
      add_pawn_moves(list, pos, sq + 9, sq, FLAG_CAPTURE);
    }

    if (pos->enpassant >= 0) {
      int ep_file = SQ_FILE(pos->enpassant);
      if (ep_file > 0) {
        int pawn_sq = SQ(ep_file - 1, 3);
        if (pawns & (1ULL << pawn_sq)) {
          add_move(list, pawn_sq, pos->enpassant, FLAG_CAPTURE);
        }
      }
      if (ep_file < 7) {
        int pawn_sq = SQ(ep_file + 1, 3);
        if (pawns & (1ULL << pawn_sq)) {
          add_move(list, pawn_sq, pos->enpassant, FLAG_CAPTURE);
        }
      }
    }
  }

  // Knights
  Bitboard knights = pos->pieces[color][KNIGHT];
  int sq;
  BB_FOREACH(sq, knights) {
    Bitboard moves = knight_attacks[sq] & enemies;
    int to;
    BB_FOREACH(to, moves) { add_move(list, sq, to, FLAG_CAPTURE); }
  }

  // Bishops
  Bitboard bishops = pos->pieces[color][BISHOP];
  BB_FOREACH(sq, bishops) {
    Bitboard moves = get_bishop_attacks(sq, pos->all) & enemies;
    int to;
    BB_FOREACH(to, moves) { add_move(list, sq, to, FLAG_CAPTURE); }
  }

  // Rooks
  Bitboard rooks = pos->pieces[color][ROOK];
  BB_FOREACH(sq, rooks) {
    Bitboard moves = get_rook_attacks(sq, pos->all) & enemies;
    int to;
    BB_FOREACH(to, moves) { add_move(list, sq, to, FLAG_CAPTURE); }
  }

  // Queens
  Bitboard queens = pos->pieces[color][QUEEN];
  BB_FOREACH(sq, queens) {
    Bitboard moves = get_queen_attacks(sq, pos->all) & enemies;
    int to;
    BB_FOREACH(to, moves) { add_move(list, sq, to, FLAG_CAPTURE); }
  }

  // King
  Bitboard kings = pos->pieces[color][KING];
  if (kings) {
    sq = LSB(kings);
    Bitboard moves = king_attacks[sq] & enemies;
    int to;
    BB_FOREACH(to, moves) { add_move(list, sq, to, FLAG_CAPTURE); }
  }
}

int movegen_is_pseudo_legal(const Position *pos, Move move) {
  int from = MOVE_FROM(move);
  int to = MOVE_TO(move);
  int color = pos->to_move;

  // Check if from square holds a piece of our color
  int piece = -1;
  for (int p = 0; p < 6; p++) {
    if (pos->pieces[color][p] & (1ULL << from)) {
      piece = p;
      break;
    }
  }

  if (piece == -1) // No piece or wrong color
    return 0;

  // Check destination (cannot capture own piece)
  if (pos->occupied[color] & (1ULL << to))
    return 0;

  Bitboard targets = 0;

  switch (piece) {
  case PAWN: {
    // Pawn logic is complex (push, double push, capture, enpassant)
    Bitboard pawn_bb = (1ULL << from);
    Bitboard empty = ~pos->all;
    Bitboard enemies = pos->occupied[color ^ 1];

    if (color == WHITE) {
      if ((pawn_bb << 8) & empty & (1ULL << to))
        return 1; // Push
      if (SQ_RANK(from) == 1 &&
          (pawn_bb << 16) & empty & ((empty & RANK_3) << 8) & (1ULL << to))
        return 1; // Double push
      if ((pawn_bb << 7) & ~FILE_H & enemies & (1ULL << to))
        return 1; // Capture Left
      if ((pawn_bb << 9) & ~FILE_A & enemies & (1ULL << to))
        return 1; // Capture Right
      // Enpassant
      if (pos->enpassant == to && pos->enpassant != -1) {
        if ((pawn_bb << 7) & ~FILE_H & (1ULL << to))
          return 1;
        if ((pawn_bb << 9) & ~FILE_A & (1ULL << to))
          return 1;
      }
    } else {
      if ((pawn_bb >> 8) & empty & (1ULL << to))
        return 1;
      if (SQ_RANK(from) == 6 &&
          (pawn_bb >> 16) & empty & ((empty & RANK_6) >> 8) & (1ULL << to))
        return 1;
      if ((pawn_bb >> 7) & ~FILE_A & enemies & (1ULL << to))
        return 1;
      if ((pawn_bb >> 9) & ~FILE_H & enemies & (1ULL << to))
        return 1;
      if (pos->enpassant == to && pos->enpassant != -1) {
        if ((pawn_bb >> 7) & ~FILE_A & (1ULL << to))
          return 1;
        if ((pawn_bb >> 9) & ~FILE_H & (1ULL << to))
          return 1;
      }
    }
    return 0;
  }
  case KNIGHT:
    targets = knight_attacks[from];
    break;
  case BISHOP:
    targets = get_bishop_attacks(from, pos->all);
    break;
  case ROOK:
    targets = get_rook_attacks(from, pos->all);
    break;
  case QUEEN:
    targets = get_queen_attacks(from, pos->all);
    break;
  case KING:
    targets = king_attacks[from];
    break;
  }

  // Check if target is in attack set
  return (targets & (1ULL << to)) ? 1 : 0;
}

int movegen_is_legal(const Position *pos, Move move) {
  Position temp = *pos;
  UndoInfo undo;
  position_make_move(&temp, move, &undo);
  // After making a move, temp.to_move is the opponent.
  // We need to check if the side that JUST MOVED left their king in check.
  // Flip to_move temporarily to check if the moving side's king is attacked.
  temp.to_move ^= 1;
  int in_check = position_in_check(&temp);
  return !in_check;
}
