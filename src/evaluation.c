#include "evaluation.h"
#include "bitboard.h"
#include "magic.h"
#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "types.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===== DRAW DETECTION CONSTANTS =====

// Material key encoding helpers
#define MATERIAL_KEY_PAWNS_MASK 0x000F
#define MATERIAL_KEY_KNIGHTS_MASK 0x00F0
#define MATERIAL_KEY_BISHOPS_MASK 0x0F00
#define MATERIAL_KEY_ROOKS_MASK 0xF000
// Note: Queens are tracked separately due to limited bits

// (get_material_key removed as it was unused)

// ===== EVALUATION CONSTANTS =====

// Knight outpost bonuses by square (for white, flip for black)
static const int knight_outpost_bonus[64] = {
    0, 0,  0,  0,  0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0,
    0, 5,  10, 15, 15, 10, 5,  0, 0, 10, 20, 25, 25, 20, 10, 0,
    0, 10, 20, 25, 25, 20, 10, 0, 0, 5,  10, 15, 15, 10, 5,  0,
    0, 0,  0,  0,  0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0};

// Center squares for control evaluation
static const Bitboard CENTER_SQUARES = (1ULL << SQ(3, 3)) | (1ULL << SQ(3, 4)) |
                                       (1ULL << SQ(4, 3)) | (1ULL << SQ(4, 4));
static const Bitboard EXTENDED_CENTER =
    (1ULL << SQ(2, 2)) | (1ULL << SQ(2, 3)) | (1ULL << SQ(2, 4)) |
    (1ULL << SQ(2, 5)) | (1ULL << SQ(3, 2)) | (1ULL << SQ(3, 5)) |
    (1ULL << SQ(4, 2)) | (1ULL << SQ(4, 5)) | (1ULL << SQ(5, 2)) |
    (1ULL << SQ(5, 3)) | (1ULL << SQ(5, 4)) | (1ULL << SQ(5, 5));

// Long diagonals for bishop evaluation
static const Bitboard LONG_DIAGONAL_A1H8 = 0x8040201008040201ULL;
static const Bitboard LONG_DIAGONAL_H1A8 = 0x0102040810204080ULL;

// Phase calculation weights
#define PHASE_KNIGHT 1
#define PHASE_BISHOP 1
#define PHASE_ROOK 2
#define PHASE_QUEEN 4
#define TOTAL_PHASE                                                            \
  (4 * PHASE_KNIGHT + 4 * PHASE_BISHOP + 4 * PHASE_ROOK + 2 * PHASE_QUEEN)

// ===== EVALUATION WEIGHTS (Opening, Endgame) =====
// These use tapered evaluation between opening and endgame

// Piece-specific
#define KNIGHT_OUTPOST_MG 20
#define KNIGHT_OUTPOST_EG 15
#define KNIGHT_MOBILITY_MG 4
#define KNIGHT_MOBILITY_EG 4
#define BISHOP_PAIR_MG 50
#define BISHOP_PAIR_EG 70
#define BISHOP_LONG_DIAG_MG 15
#define BISHOP_LONG_DIAG_EG 10
#define BAD_BISHOP_MG 10
#define BAD_BISHOP_EG 15
#define ROOK_OPEN_FILE_MG 20
#define ROOK_OPEN_FILE_EG 15
#define ROOK_SEMI_OPEN_MG 10
#define ROOK_SEMI_OPEN_EG 8
#define ROOK_7TH_RANK_MG 20
#define ROOK_7TH_RANK_EG 30
#define ROOK_CONNECTED_MG 10
#define ROOK_CONNECTED_EG 5
#define QUEEN_MOBILITY_MG 2
#define QUEEN_MOBILITY_EG 4
#define QUEEN_EARLY_DEV_MG 20
#define QUEEN_EARLY_DEV_EG 0

// Pawn structure
#define DOUBLED_PAWN_MG 15
#define DOUBLED_PAWN_EG 25
#define ISOLATED_PAWN_MG 15
#define ISOLATED_PAWN_EG 20
#define BACKWARD_PAWN_MG 12
#define BACKWARD_PAWN_EG 15
#define HANGING_PAWN_MG 8
#define HANGING_PAWN_EG 10
#define PAWN_CHAIN_MG 5
#define PAWN_CHAIN_EG 3
#define PASSED_PAWN_BASE_MG 10
#define PASSED_PAWN_BASE_EG 20
#define PROTECTED_PASSED_MG 15
#define PROTECTED_PASSED_EG 25
#define OUTSIDE_PASSED_MG 10
#define OUTSIDE_PASSED_EG 30
#define CANDIDATE_PASSED_MG 8
#define CANDIDATE_PASSED_EG 15
#define PAWN_ISLAND_MG 5
#define PAWN_ISLAND_EG 8

// King safety
#define KING_SHELTER_MG 10
#define KING_SHELTER_EG 0
#define KING_OPEN_FILE_MG 15
#define KING_OPEN_FILE_EG 5
#define PAWN_STORM_MG 8
#define PAWN_STORM_EG 0

// Positional
#define CENTER_CONTROL_MG 8
#define CENTER_CONTROL_EG 4
#define SPACE_BONUS_MG 3
#define SPACE_BONUS_EG 1
#define DEVELOPMENT_MG 10
#define DEVELOPMENT_EG 0
#define PIECE_COORD_MG 5
#define PIECE_COORD_EG 3

// Piece-square tables (simplified, for opening phase)
static const int pawn_table[64] = {0,  0,  0,  0,  0,  0,  0,  0,  // Rank 1
                                   5,  5,  5,  -5, -5, 5,  5,  5,  // Rank 2
                                   5,  5,  10, 15, 15, 10, 5,  5,  // Rank 3
                                   5,  5,  15, 25, 25, 15, 5,  5,  // Rank 4
                                   10, 10, 20, 30, 30, 20, 10, 10, // Rank 5
                                   30, 30, 40, 50, 50, 40, 30, 30, // Rank 6
                                   70, 70, 70, 70, 70, 70, 70, 70, // Rank 7
                                   0,  0,  0,  0,  0,  0,  0,  0}; // Rank 8

static const int knight_table[64] = {
    -50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   0,   0,
    0,   -20, -40, -30, 0,   10,  15,  15,  10,  0,   -30, -30, 5,
    15,  20,  20,  15,  5,   -30, -30, 0,   15,  20,  20,  15,  0,
    -30, -30, 5,   10,  15,  15,  10,  5,   -30, -40, -20, 0,   5,
    5,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50};

static const int bishop_table[64] = {
    -20, -10, -10, -10, -10, -10, -10, -20, -10, 0,   0,   0,   0,
    0,   0,   -10, -10, 0,   5,   10,  10,  5,   0,   -10, -10, 5,
    5,   10,  10,  5,   5,   -10, -10, 0,   10,  10,  10,  10,  0,
    -10, -10, 10,  10,  10,  10,  10,  10,  -10, -10, 5,   0,   0,
    0,   0,   5,   -10, -20, -10, -10, -10, -10, -10, -10, -20};

static const int rook_table[64] = {
    0,  0, 0, 0, 0, 0, 0, 0,  5,  10, 10, 10, 10, 10, 10, 5,
    -5, 0, 0, 0, 0, 0, 0, -5, -5, 0,  0,  0,  0,  0,  0,  -5,
    -5, 0, 0, 0, 0, 0, 0, -5, -5, 0,  0,  0,  0,  0,  0,  -5,
    -5, 0, 0, 0, 0, 0, 0, -5, 0,  0,  0,  5,  5,  0,  0,  0};

static const int queen_table[64] = {
    -20, -10, -10, -5, -5, -10, -10, -20, -10, 0,   0,   0,  0,  0,   0,   -10,
    -10, 0,   5,   5,  5,  5,   0,   -10, -5,  0,   5,   5,  5,  5,   0,   -5,
    0,   0,   5,   5,  5,  5,   0,   -5,  -10, 5,   5,   5,  5,  5,   0,   -10,
    -10, 0,   5,   0,  0,  0,   0,   -10, -20, -10, -10, -5, -5, -10, -10, -20};

static const int king_table_opening[64] = {
    -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50,
    -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40,
    -40, -50, -50, -40, -40, -30, -20, -30, -30, -40, -40, -30, -30,
    -20, -10, -20, -20, -20, -20, -20, -20, -10, 20,  20,  0,   0,
    0,   0,   20,  20,  20,  30,  10,  0,   0,   10,  30,  20};

static const int king_table_endgame[64] = {
    -50, -40, -30, -20, -20, -30, -40, -50, -30, -20, -10, 0,   0,
    -10, -20, -30, -30, -10, 20,  30,  30,  20,  -10, -30, -30, -10,
    30,  40,  40,  30,  -10, -30, -30, -10, 30,  40,  40,  30,  -10,
    -30, -30, -10, 20,  30,  30,  20,  -10, -30, -30, -20, -10, 0,
    0,   -10, -20, -30, -50, -40, -30, -20, -20, -30, -40, -50};

static int material(const Position *pos, int color) {
  int total = 0;
  total += POPCOUNT(pos->pieces[color][PAWN]) * VALUE_PAWN;
  total += POPCOUNT(pos->pieces[color][KNIGHT]) * VALUE_KNIGHT;
  total += POPCOUNT(pos->pieces[color][BISHOP]) * VALUE_BISHOP;
  total += POPCOUNT(pos->pieces[color][ROOK]) * VALUE_ROOK;
  total += POPCOUNT(pos->pieces[color][QUEEN]) * VALUE_QUEEN;
  return total;
}

static int piece_square(const Position *pos, int color) {
  int total = 0;

  Bitboard bb = pos->pieces[color][PAWN];
  int sq;
  BB_FOREACH(sq, bb) {
    int idx = color == WHITE ? sq : 63 - sq;
    total += pawn_table[idx];
  }

  bb = pos->pieces[color][KNIGHT];
  BB_FOREACH(sq, bb) {
    int idx = color == WHITE ? sq : 63 - sq;
    total += knight_table[idx];
  }

  bb = pos->pieces[color][BISHOP];
  BB_FOREACH(sq, bb) {
    int idx = color == WHITE ? sq : 63 - sq;
    total += bishop_table[idx];
  }

  bb = pos->pieces[color][ROOK];
  BB_FOREACH(sq, bb) {
    int idx = color == WHITE ? sq : 63 - sq;
    total += rook_table[idx];
  }

  bb = pos->pieces[color][QUEEN];
  BB_FOREACH(sq, bb) {
    int idx = color == WHITE ? sq : 63 - sq;
    total += queen_table[idx];
  }
  return total;
}

int phase_eval(const Position *pos) {
  int material_white = material(pos, WHITE);
  int material_black = material(pos, BLACK);
  int max_material = 39 * 100 + 10 * 320 + 10 * 330 + 10 * 500 + 9 * 900;
  int material_sum = material_white + material_black;
  return (material_sum * 256) / max_material;
}

Score evaluate(const Position *pos) {
  // NNUE Evaluation
  if (uci_use_nnue && nnue_available) {
    return nnue_evaluate(pos);
  }

  // Material evaluation
  int mat_white = material(pos, WHITE);
  int mat_black = material(pos, BLACK);
  int material_score = mat_white - mat_black;

  // Piece-square tables
  int psq_white = piece_square(pos, WHITE);
  int psq_black = piece_square(pos, BLACK);
  int psq_score = psq_white - psq_black;

  // Game phase (0 = endgame, 256 = opening)
  int phase = phase_eval(pos);

  // ===== TAPERED EVALUATION =====
  // Blend opening and endgame scores based on game phase

  Score material_tapered = material_score; // Material is constant
  Score psq_tapered = (psq_score * phase) / 256;

  // ===== PIECE-SPECIFIC EVALUATION =====

  // Knight outposts
  Score knight_outpost_bonus = evaluate_knight_outposts(pos);

  // Bishop evaluation (bad bishop, long diagonal)
  Score bishop_bonus = evaluate_bishops(pos);

  // Bishop pair bonus
  Score bishop_pair_bonus = evaluate_bishop_pair(pos);

  // Rook evaluation (open files - existing)
  Score rook_files_bonus = evaluate_rook_files(pos);
  rook_files_bonus = (rook_files_bonus * phase) / 256;

  // Rook advanced evaluation (7th rank, connected)
  Score rook_advanced_bonus = evaluate_rooks_advanced(pos);

  // Queen evaluation
  Score queen_bonus = evaluate_queen(pos);

  // ===== PAWN STRUCTURE =====

  // Basic pawn structure (doubled, isolated, backward, passed)
  Score pawn_bonus = evaluate_pawns(pos);
  pawn_bonus = (pawn_bonus * (256 - phase / 2)) / 256;

  // Advanced pawn structure (chains, hanging, islands, candidates)
  Score pawn_advanced_bonus = evaluate_pawns_advanced(pos);

  // Advanced passed pawn evaluation
  Score passed_pawn_bonus = evaluate_passed_pawns_advanced(pos);

  // ===== KING SAFETY =====
  Score king_bonus = evaluate_king_safety(pos);
  king_bonus = (king_bonus * (256 - phase / 2)) / 256;

  // ===== POSITIONAL FEATURES =====

  // Space advantage
  Score space_bonus = evaluate_space(pos);

  // Center control
  Score center_bonus = evaluate_center_control(pos);

  // Development (opening only)
  Score development_bonus = evaluate_development(pos);

  // ===== ENDGAME KNOWLEDGE =====
  Score endgame_bonus = evaluate_endgame_knowledge(pos);

  // ===== FINAL SCORE =====
  Score final_score =
      material_tapered + psq_tapered + knight_outpost_bonus + bishop_bonus +
      bishop_pair_bonus + rook_files_bonus + rook_advanced_bonus + queen_bonus +
      pawn_bonus + pawn_advanced_bonus + passed_pawn_bonus + king_bonus +
      space_bonus + center_bonus + development_bonus + endgame_bonus;

  return pos->to_move == WHITE ? final_score : -final_score;
}

Score evaluate_mobility(const Position *pos) {
  // Generate all moves for white
  Position temp_white = *pos;
  temp_white.to_move = WHITE;
  MoveList white_moves;
  movegen_all(&temp_white, &white_moves);
  int white_legal_count = 0;
  for (int i = 0; i < white_moves.count; i++) {
    if (movegen_is_legal(&temp_white, white_moves.moves[i])) {
      white_legal_count++;
    }
  }

  // Generate all moves for black
  Position temp_black = *pos;
  temp_black.to_move = BLACK;
  MoveList black_moves;
  movegen_all(&temp_black, &black_moves);
  int black_legal_count = 0;
  for (int i = 0; i < black_moves.count; i++) {
    if (movegen_is_legal(&temp_black, black_moves.moves[i])) {
      black_legal_count++;
    }
  }

  // Mobility bonus: (2-3 cp per move) with phase scaling
  int phase = phase_eval(pos);
  Score mobility_bonus = (white_legal_count - black_legal_count) * 3;
  return (mobility_bonus * phase) / 256;
}

Score evaluate_pawns(const Position *pos) {
  Score score = 0;

  // Doubled pawns penalty
  for (int file = 0; file < 8; file++) {
    Bitboard file_mask = FILE_A << file;
    int white_pawns = POPCOUNT(pos->pieces[WHITE][PAWN] & file_mask);
    int black_pawns = POPCOUNT(pos->pieces[BLACK][PAWN] & file_mask);

    if (white_pawns > 1)
      score -= (white_pawns - 1) * 20;
    if (black_pawns > 1)
      score += (black_pawns - 1) * 20;
  }

  // Isolated pawns penalty
  for (int file = 0; file < 8; file++) {
    Bitboard adj_files = (file > 0 ? (FILE_A << (file - 1)) : 0) |
                         (file < 7 ? (FILE_A << (file + 1)) : 0);
    Bitboard current = FILE_A << file;

    int white_pawns = POPCOUNT(pos->pieces[WHITE][PAWN] & current);
    int black_pawns = POPCOUNT(pos->pieces[BLACK][PAWN] & current);

    int white_neighbors = POPCOUNT(pos->pieces[WHITE][PAWN] & adj_files);
    int black_neighbors = POPCOUNT(pos->pieces[BLACK][PAWN] & adj_files);

    if (white_pawns > 0 && white_neighbors == 0)
      score -= 15; // Isolated pawn
    if (black_pawns > 0 && black_neighbors == 0)
      score += 15; // Isolated pawn
  }

  // Backward pawns penalty - pawn that cannot advance safely
  // and has no friendly pawns on adjacent files that are behind it
  Bitboard white_pawns = pos->pieces[WHITE][PAWN];
  Bitboard black_pawns = pos->pieces[BLACK][PAWN];

  int sq;
  BB_FOREACH(sq, white_pawns) {
    int file = SQ_FILE(sq);
    int rank = SQ_RANK(sq);

    // Check if pawn can be supported by adjacent pawns
    Bitboard supporters = 0;
    if (file > 0)
      supporters |= pos->pieces[WHITE][PAWN] & (FILE_A << (file - 1));
    if (file < 7)
      supporters |= pos->pieces[WHITE][PAWN] & (FILE_A << (file + 1));

    // Get pawns that are behind this one (on same or lower rank)
    Bitboard behind_mask = 0;
    for (int r = 0; r <= rank; r++)
      behind_mask |= (0xFFULL << (r * 8));

    if ((supporters & behind_mask) == 0 && rank > 1 && rank < 6) {
      // No supporting pawns behind - check if stop square is controlled by
      // enemy pawn
      Bitboard enemy_control = 0;
      if (file > 0)
        enemy_control |= (1ULL << SQ(file - 1, rank + 2));
      if (file < 7)
        enemy_control |= (1ULL << SQ(file + 1, rank + 2));

      if (rank + 2 < 8 && (black_pawns & enemy_control)) {
        score -= 12; // Backward pawn
      }
    }
  }

  BB_FOREACH(sq, black_pawns) {
    int file = SQ_FILE(sq);
    int rank = SQ_RANK(sq);

    // Check if pawn can be supported by adjacent pawns
    Bitboard supporters = 0;
    if (file > 0)
      supporters |= pos->pieces[BLACK][PAWN] & (FILE_A << (file - 1));
    if (file < 7)
      supporters |= pos->pieces[BLACK][PAWN] & (FILE_A << (file + 1));

    // Get pawns that are behind this one (on same or higher rank for black)
    Bitboard behind_mask = 0;
    for (int r = rank; r < 8; r++)
      behind_mask |= (0xFFULL << (r * 8));

    if ((supporters & behind_mask) == 0 && rank > 1 && rank < 6) {
      // No supporting pawns behind - check if stop square is controlled by
      // enemy pawn
      Bitboard enemy_control = 0;
      if (file > 0 && rank - 2 >= 0)
        enemy_control |= (1ULL << SQ(file - 1, rank - 2));
      if (file < 7 && rank - 2 >= 0)
        enemy_control |= (1ULL << SQ(file + 1, rank - 2));

      if (rank - 2 >= 0 && (white_pawns & enemy_control)) {
        score += 12; // Backward pawn
      }
    }
  }

  // Passed pawns bonus
  for (int rank = 1; rank < 7; rank++) {
    for (int file = 0; file < 8; file++) {
      int sq = SQ(file, rank);

      if (pos->pieces[WHITE][PAWN] & (1ULL << sq)) {
        int passed = 1;
        for (int r = rank + 1; r < 8; r++) {
          if ((pos->pieces[BLACK][PAWN] & (1ULL << SQ(file, r))) ||
              (file > 0 &&
               (pos->pieces[BLACK][PAWN] & (1ULL << SQ(file - 1, r)))) ||
              (file < 7 &&
               (pos->pieces[BLACK][PAWN] & (1ULL << SQ(file + 1, r))))) {
            passed = 0;
            break;
          }
        }
        if (passed) {
          // Bonus increases as pawn advances (rank 2-7 = bonus 10-60)
          int bonus = (rank - 1) * 10 + 10;
          score += bonus;
        }
      }

      if (pos->pieces[BLACK][PAWN] & (1ULL << sq)) {
        int passed = 1;
        for (int r = rank - 1; r >= 0; r--) {
          if ((pos->pieces[WHITE][PAWN] & (1ULL << SQ(file, r))) ||
              (file > 0 &&
               (pos->pieces[WHITE][PAWN] & (1ULL << SQ(file - 1, r)))) ||
              (file < 7 &&
               (pos->pieces[WHITE][PAWN] & (1ULL << SQ(file + 1, r))))) {
            passed = 0;
            break;
          }
        }
        if (passed) {
          // Bonus increases as pawn advances (rank 7-2 = bonus 10-60)
          int bonus = (6 - rank) * 10 + 10;
          score -= bonus;
        }
      }
    }
  }

  return score;
}

Score evaluate_king_safety(const Position *pos) {
  Score score = 0;

  Bitboard white_king = pos->pieces[WHITE][KING];
  Bitboard black_king = pos->pieces[BLACK][KING];

  if (white_king) {
    int king_sq = LSB(white_king);
    int king_file = SQ_FILE(king_sq);
    int king_rank = SQ_RANK(king_sq);

    // Pawn shelter bonus
    if (king_file <= 2) {
      // Queenside
      int shelter = POPCOUNT((pos->pieces[WHITE][PAWN] & (RANK_2 | RANK_3)) &
                             ((FILE_A | FILE_B | FILE_C)));
      score += shelter * 8;
    } else if (king_file >= 5) {
      // Kingside
      int shelter = POPCOUNT((pos->pieces[WHITE][PAWN] & (RANK_2 | RANK_3)) &
                             ((FILE_F | FILE_G | FILE_H)));
      score += shelter * 8;
    } else {
      // King in center - penalty in middlegame
      int phase = phase_eval(pos);
      score -= (phase * 30) / 256;
    }

    // Open file penalty near king
    for (int f = (king_file > 0 ? king_file - 1 : 0);
         f <= (king_file < 7 ? king_file + 1 : 7); f++) {
      Bitboard file_mask = FILE_A << f;
      if ((pos->pieces[WHITE][PAWN] & file_mask) == 0) {
        score -= 10; // Open file near king
        if ((pos->pieces[BLACK][ROOK] & file_mask) ||
            (pos->pieces[BLACK][QUEEN] & file_mask)) {
          score -= 15; // Enemy heavy piece on open file near king
        }
      }
    }

    // Penalty for missing pawn directly in front of king
    if (king_rank < 7) {
      int front_sq = SQ(king_file, king_rank + 1);
      if (!(pos->pieces[WHITE][PAWN] & (1ULL << front_sq))) {
        score -= 10;
      }
    }
  }

  if (black_king) {
    int king_sq = LSB(black_king);
    int king_file = SQ_FILE(king_sq);
    int king_rank = SQ_RANK(king_sq);

    // Pawn shelter bonus
    if (king_file <= 2) {
      // Queenside
      int shelter = POPCOUNT((pos->pieces[BLACK][PAWN] & (RANK_6 | RANK_7)) &
                             ((FILE_A | FILE_B | FILE_C)));
      score -= shelter * 8;
    } else if (king_file >= 5) {
      // Kingside
      int shelter = POPCOUNT((pos->pieces[BLACK][PAWN] & (RANK_6 | RANK_7)) &
                             ((FILE_F | FILE_G | FILE_H)));
      score -= shelter * 8;
    } else {
      // King in center - penalty in middlegame
      int phase = phase_eval(pos);
      score += (phase * 30) / 256;
    }

    // Open file penalty near king
    for (int f = (king_file > 0 ? king_file - 1 : 0);
         f <= (king_file < 7 ? king_file + 1 : 7); f++) {
      Bitboard file_mask = FILE_A << f;
      if ((pos->pieces[BLACK][PAWN] & file_mask) == 0) {
        score += 10; // Open file near king
        if ((pos->pieces[WHITE][ROOK] & file_mask) ||
            (pos->pieces[WHITE][QUEEN] & file_mask)) {
          score += 15; // Enemy heavy piece on open file near king
        }
      }
    }

    // Penalty for missing pawn directly in front of king
    if (king_rank > 0) {
      int front_sq = SQ(king_file, king_rank - 1);
      if (!(pos->pieces[BLACK][PAWN] & (1ULL << front_sq))) {
        score += 10;
      }
    }
  }

  return score;
}

Score evaluate_bishop_pair(const Position *pos) {
  Score score = 0;

  // Bishop pair bonus: +50 cp in opening, +30 cp in endgame
  int white_bishops = POPCOUNT(pos->pieces[WHITE][BISHOP]);
  int black_bishops = POPCOUNT(pos->pieces[BLACK][BISHOP]);

  int phase = phase_eval(pos);
  int opening_bonus = 50;
  int endgame_bonus = 30;

  if (white_bishops >= 2) {
    score += (opening_bonus * (256 - phase) + endgame_bonus * phase) / 256;
  }
  if (black_bishops >= 2) {
    score -= (opening_bonus * (256 - phase) + endgame_bonus * phase) / 256;
  }

  return score;
}

Score evaluate_rook_files(const Position *pos) {
  Score score = 0;

  // Rook on open/semi-open files: +10-20 cp per rook
  for (int file = 0; file < 8; file++) {
    Bitboard file_mask = FILE_A << file;

    // Check white rooks on this file
    Bitboard white_rooks = pos->pieces[WHITE][ROOK] & file_mask;
    if (white_rooks) {
      int white_pawns = POPCOUNT(pos->pieces[WHITE][PAWN] & file_mask);
      int black_pawns = POPCOUNT(pos->pieces[BLACK][PAWN] & file_mask);

      if (white_pawns == 0) {
        if (black_pawns == 0)
          score += 15; // Open file
        else
          score += 10; // Semi-open file
      }
    }

    // Check black rooks on this file
    Bitboard black_rooks = pos->pieces[BLACK][ROOK] & file_mask;
    if (black_rooks) {
      int white_pawns = POPCOUNT(pos->pieces[WHITE][PAWN] & file_mask);
      int black_pawns = POPCOUNT(pos->pieces[BLACK][PAWN] & file_mask);

      if (black_pawns == 0) {
        if (white_pawns == 0)
          score -= 15; // Open file
        else
          score -= 10; // Semi-open file
      }
    }
  }
  return score;
}

// ===== ENHANCED PIECE EVALUATION =====

// External knight attacks table from movegen.c
extern Bitboard knight_attacks[64];
extern Bitboard king_attacks[64];

// Evaluate knight outposts - knights on squares protected by pawns and not
// attackable by enemy pawns
Score evaluate_knight_outposts(const Position *pos) {
  Score score = 0;
  int phase = phase_eval(pos);

  // White knights
  Bitboard white_knights = pos->pieces[WHITE][KNIGHT];
  int sq;
  BB_FOREACH(sq, white_knights) {
    int rank = SQ_RANK(sq);
    int file = SQ_FILE(sq);

    // Check if knight is on an outpost (rank 4-6 for white)
    if (rank >= 3 && rank <= 5) {
      // Check if protected by white pawn
      Bitboard pawn_defenders = 0;
      if (file > 0 && rank > 0)
        pawn_defenders |= (1ULL << SQ(file - 1, rank - 1));
      if (file < 7 && rank > 0)
        pawn_defenders |= (1ULL << SQ(file + 1, rank - 1));

      if (pos->pieces[WHITE][PAWN] & pawn_defenders) {
        // Check if no enemy pawns can attack it
        Bitboard enemy_pawn_attacks = 0;
        for (int r = rank + 1; r < 8; r++) {
          if (file > 0)
            enemy_pawn_attacks |= (1ULL << SQ(file - 1, r));
          if (file < 7)
            enemy_pawn_attacks |= (1ULL << SQ(file + 1, r));
        }

        if (!(pos->pieces[BLACK][PAWN] & enemy_pawn_attacks)) {
          int idx = sq; // Use actual square for white
          int bonus_mg = knight_outpost_bonus[idx];
          int bonus_eg = knight_outpost_bonus[idx] * 3 / 4;
          score += (bonus_mg * phase + bonus_eg * (256 - phase)) / 256;
        }
      }
    }
  }

  // Black knights
  Bitboard black_knights = pos->pieces[BLACK][KNIGHT];
  BB_FOREACH(sq, black_knights) {
    int rank = SQ_RANK(sq);
    int file = SQ_FILE(sq);

    // Check if knight is on an outpost (rank 3-5 for black, i.e., rank 2-4 in
    // 0-indexed)
    if (rank >= 2 && rank <= 4) {
      // Check if protected by black pawn
      Bitboard pawn_defenders = 0;
      if (file > 0 && rank < 7)
        pawn_defenders |= (1ULL << SQ(file - 1, rank + 1));
      if (file < 7 && rank < 7)
        pawn_defenders |= (1ULL << SQ(file + 1, rank + 1));

      if (pos->pieces[BLACK][PAWN] & pawn_defenders) {
        // Check if no enemy pawns can attack it
        Bitboard enemy_pawn_attacks = 0;
        for (int r = rank - 1; r >= 0; r--) {
          if (file > 0)
            enemy_pawn_attacks |= (1ULL << SQ(file - 1, r));
          if (file < 7)
            enemy_pawn_attacks |= (1ULL << SQ(file + 1, r));
        }

        if (!(pos->pieces[WHITE][PAWN] & enemy_pawn_attacks)) {
          int idx = 63 - sq; // Flip for black
          int bonus_mg = knight_outpost_bonus[idx];
          int bonus_eg = knight_outpost_bonus[idx] * 3 / 4;
          score -= (bonus_mg * phase + bonus_eg * (256 - phase)) / 256;
        }
      }
    }
  }

  return score;
}

// Evaluate bishops - bad bishop penalty, long diagonal control
Score evaluate_bishops(const Position *pos) {
  Score score = 0;
  int phase = phase_eval(pos);

  // White bishops
  Bitboard white_bishops = pos->pieces[WHITE][BISHOP];
  int sq;
  BB_FOREACH(sq, white_bishops) {
    // Long diagonal bonus
    Bitboard attacks = get_bishop_attacks(sq, pos->all);
    if ((1ULL << sq) & (LONG_DIAGONAL_A1H8 | LONG_DIAGONAL_H1A8)) {
      int center_squares_attacked = POPCOUNT(attacks & CENTER_SQUARES);
      int bonus = center_squares_attacked * BISHOP_LONG_DIAG_MG;
      score += (bonus * phase) / 256;
    }

    // Bad bishop penalty - many own pawns on same color squares
    int is_light_sq = ((SQ_FILE(sq) + SQ_RANK(sq)) % 2);
    Bitboard same_color_squares = 0;
    for (int s = 0; s < 64; s++) {
      if (((SQ_FILE(s) + SQ_RANK(s)) % 2) == is_light_sq)
        same_color_squares |= (1ULL << s);
    }
    int blocked_pawns = POPCOUNT(pos->pieces[WHITE][PAWN] & same_color_squares);
    if (blocked_pawns >= 4) {
      int penalty_mg = (blocked_pawns - 3) * BAD_BISHOP_MG;
      int penalty_eg = (blocked_pawns - 3) * BAD_BISHOP_EG;
      score -= (penalty_mg * phase + penalty_eg * (256 - phase)) / 256;
    }
  }

  // Black bishops
  Bitboard black_bishops = pos->pieces[BLACK][BISHOP];
  BB_FOREACH(sq, black_bishops) {
    // Long diagonal bonus
    Bitboard attacks = get_bishop_attacks(sq, pos->all);
    if ((1ULL << sq) & (LONG_DIAGONAL_A1H8 | LONG_DIAGONAL_H1A8)) {
      int center_squares_attacked = POPCOUNT(attacks & CENTER_SQUARES);
      int bonus = center_squares_attacked * BISHOP_LONG_DIAG_MG;
      score -= (bonus * phase) / 256;
    }

    // Bad bishop penalty
    int is_light_sq = ((SQ_FILE(sq) + SQ_RANK(sq)) % 2);
    Bitboard same_color_squares = 0;
    for (int s = 0; s < 64; s++) {
      if (((SQ_FILE(s) + SQ_RANK(s)) % 2) == is_light_sq)
        same_color_squares |= (1ULL << s);
    }
    int blocked_pawns = POPCOUNT(pos->pieces[BLACK][PAWN] & same_color_squares);
    if (blocked_pawns >= 4) {
      int penalty_mg = (blocked_pawns - 3) * BAD_BISHOP_MG;
      int penalty_eg = (blocked_pawns - 3) * BAD_BISHOP_EG;
      score += (penalty_mg * phase + penalty_eg * (256 - phase)) / 256;
    }
  }

  return score;
}

// Evaluate rooks - 7th rank bonus, connected rooks, rook behind passed pawn
Score evaluate_rooks_advanced(const Position *pos) {
  Score score = 0;
  int phase = phase_eval(pos);

  Bitboard white_rooks = pos->pieces[WHITE][ROOK];
  Bitboard black_rooks = pos->pieces[BLACK][ROOK];

  // White rooks
  int sq;
  BB_FOREACH(sq, white_rooks) {
    int rank = SQ_RANK(sq);

    // Rook on 7th rank bonus (especially if enemy king on 8th)
    if (rank == 6) {
      int bonus_mg = ROOK_7TH_RANK_MG;
      int bonus_eg = ROOK_7TH_RANK_EG;

      // Extra bonus if enemy king on 8th rank
      if (pos->pieces[BLACK][KING] &&
          SQ_RANK(LSB(pos->pieces[BLACK][KING])) == 7) {
        bonus_mg += 10;
        bonus_eg += 15;
      }
      score += (bonus_mg * phase + bonus_eg * (256 - phase)) / 256;
    }
  }

  // Connected rooks bonus (rooks defending each other)
  if (POPCOUNT(white_rooks) >= 2) {
    Bitboard temp = white_rooks;
    int r1 = pop_lsb(&temp);
    int r2 = pop_lsb(&temp);

    // Check if on same rank or file
    if (SQ_RANK(r1) == SQ_RANK(r2) || SQ_FILE(r1) == SQ_FILE(r2)) {
      // Check no pieces between them
      Bitboard between = 0;
      if (SQ_RANK(r1) == SQ_RANK(r2)) {
        int min_f = SQ_FILE(r1) < SQ_FILE(r2) ? SQ_FILE(r1) : SQ_FILE(r2);
        int max_f = SQ_FILE(r1) > SQ_FILE(r2) ? SQ_FILE(r1) : SQ_FILE(r2);
        for (int f = min_f + 1; f < max_f; f++)
          between |= (1ULL << SQ(f, SQ_RANK(r1)));
      } else {
        int min_r = SQ_RANK(r1) < SQ_RANK(r2) ? SQ_RANK(r1) : SQ_RANK(r2);
        int max_r = SQ_RANK(r1) > SQ_RANK(r2) ? SQ_RANK(r1) : SQ_RANK(r2);
        for (int r = min_r + 1; r < max_r; r++)
          between |= (1ULL << SQ(SQ_FILE(r1), r));
      }

      if (!(pos->all & between)) {
        score +=
            (ROOK_CONNECTED_MG * phase + ROOK_CONNECTED_EG * (256 - phase)) /
            256;
      }
    }
  }

  // Black rooks
  BB_FOREACH(sq, black_rooks) {
    int rank = SQ_RANK(sq);

    // Rook on 2nd rank bonus (7th from black's perspective)
    if (rank == 1) {
      int bonus_mg = ROOK_7TH_RANK_MG;
      int bonus_eg = ROOK_7TH_RANK_EG;

      if (pos->pieces[WHITE][KING] &&
          SQ_RANK(LSB(pos->pieces[WHITE][KING])) == 0) {
        bonus_mg += 10;
        bonus_eg += 15;
      }
      score -= (bonus_mg * phase + bonus_eg * (256 - phase)) / 256;
    }
  }

  // Connected black rooks
  if (POPCOUNT(black_rooks) >= 2) {
    Bitboard temp = black_rooks;
    int r1 = pop_lsb(&temp);
    int r2 = pop_lsb(&temp);

    if (SQ_RANK(r1) == SQ_RANK(r2) || SQ_FILE(r1) == SQ_FILE(r2)) {
      Bitboard between = 0;
      if (SQ_RANK(r1) == SQ_RANK(r2)) {
        int min_f = SQ_FILE(r1) < SQ_FILE(r2) ? SQ_FILE(r1) : SQ_FILE(r2);
        int max_f = SQ_FILE(r1) > SQ_FILE(r2) ? SQ_FILE(r1) : SQ_FILE(r2);
        for (int f = min_f + 1; f < max_f; f++)
          between |= (1ULL << SQ(f, SQ_RANK(r1)));
      } else {
        int min_r = SQ_RANK(r1) < SQ_RANK(r2) ? SQ_RANK(r1) : SQ_RANK(r2);
        int max_r = SQ_RANK(r1) > SQ_RANK(r2) ? SQ_RANK(r1) : SQ_RANK(r2);
        for (int r = min_r + 1; r < max_r; r++)
          between |= (1ULL << SQ(SQ_FILE(r1), r));
      }

      if (!(pos->all & between)) {
        score -=
            (ROOK_CONNECTED_MG * phase + ROOK_CONNECTED_EG * (256 - phase)) /
            256;
      }
    }
  }

  return score;
}

// Evaluate queen - mobility, proximity to enemy king, early development
// penalty
Score evaluate_queen(const Position *pos) {
  Score score = 0;
  int phase = phase_eval(pos);

  // White queen
  Bitboard white_queen = pos->pieces[WHITE][QUEEN];
  if (white_queen) {
    int sq = LSB(white_queen);

    // Queen mobility
    Bitboard attacks = get_queen_attacks(sq, pos->all) & ~pos->occupied[WHITE];
    int mobility = POPCOUNT(attacks);
    score += (mobility * QUEEN_MOBILITY_MG * phase +
              mobility * QUEEN_MOBILITY_EG * (256 - phase)) /
             256;

    // Proximity to enemy king
    if (pos->pieces[BLACK][KING]) {
      int enemy_king_sq = LSB(pos->pieces[BLACK][KING]);
      int dist = abs(SQ_FILE(sq) - SQ_FILE(enemy_king_sq)) +
                 abs(SQ_RANK(sq) - SQ_RANK(enemy_king_sq));
      // Closer = better, max bonus when adjacent
      int tropism_bonus = (14 - dist) * 2;
      score += (tropism_bonus * phase) / 256;
    }

    // Early queen development penalty (if minor pieces still on back rank)
    if (phase > 200) // Still in opening
    {
      int undeveloped = 0;
      // Check if knights/bishops still on starting squares
      if (pos->pieces[WHITE][KNIGHT] & (1ULL << SQ(1, 0)))
        undeveloped++;
      if (pos->pieces[WHITE][KNIGHT] & (1ULL << SQ(6, 0)))
        undeveloped++;
      if (pos->pieces[WHITE][BISHOP] & (1ULL << SQ(2, 0)))
        undeveloped++;
      if (pos->pieces[WHITE][BISHOP] & (1ULL << SQ(5, 0)))
        undeveloped++;

      if (undeveloped >= 2 && SQ_RANK(sq) > 1) {
        score -= QUEEN_EARLY_DEV_MG;
      }
    }
  }

  // Black queen
  Bitboard black_queen = pos->pieces[BLACK][QUEEN];
  if (black_queen) {
    int sq = LSB(black_queen);

    // Queen mobility
    Bitboard attacks = get_queen_attacks(sq, pos->all) & ~pos->occupied[BLACK];
    int mobility = POPCOUNT(attacks);
    score -= (mobility * QUEEN_MOBILITY_MG * phase +
              mobility * QUEEN_MOBILITY_EG * (256 - phase)) /
             256;

    // Proximity to enemy king
    if (pos->pieces[WHITE][KING]) {
      int enemy_king_sq = LSB(pos->pieces[WHITE][KING]);
      int dist = abs(SQ_FILE(sq) - SQ_FILE(enemy_king_sq)) +
                 abs(SQ_RANK(sq) - SQ_RANK(enemy_king_sq));
      int tropism_bonus = (14 - dist) * 2;
      score -= (tropism_bonus * phase) / 256;
    }

    // Early queen development penalty
    if (phase > 200) {
      int undeveloped = 0;
      if (pos->pieces[BLACK][KNIGHT] & (1ULL << SQ(1, 7)))
        undeveloped++;
      if (pos->pieces[BLACK][KNIGHT] & (1ULL << SQ(6, 7)))
        undeveloped++;
      if (pos->pieces[BLACK][BISHOP] & (1ULL << SQ(2, 7)))
        undeveloped++;
      if (pos->pieces[BLACK][BISHOP] & (1ULL << SQ(5, 7)))
        undeveloped++;

      if (undeveloped >= 2 && SQ_RANK(sq) < 6) {
        score += QUEEN_EARLY_DEV_MG;
      }
    }
  }

  return score;
}

// ===== ADVANCED PAWN STRUCTURE =====

// Count pawn islands for a color
static int count_pawn_islands(Bitboard pawns) {
  int islands = 0;
  int in_island = 0;

  for (int file = 0; file < 8; file++) {
    Bitboard file_mask = FILE_A << file;
    if (pawns & file_mask) {
      if (!in_island) {
        islands++;
        in_island = 1;
      }
    } else {
      in_island = 0;
    }
  }
  return islands;
}

// Evaluate advanced pawn structure - chains, hanging pawns, islands,
// candidate passed
Score evaluate_pawns_advanced(const Position *pos) {
  Score score = 0;
  int phase = phase_eval(pos);

  Bitboard white_pawns = pos->pieces[WHITE][PAWN];
  Bitboard black_pawns = pos->pieces[BLACK][PAWN];

  // Pawn chains - pawns defended by other pawns
  int sq;
  BB_FOREACH(sq, white_pawns) {
    int file = SQ_FILE(sq);
    int rank = SQ_RANK(sq);

    // Check if defended by another pawn
    Bitboard defenders = 0;
    if (file > 0 && rank > 0)
      defenders |= (1ULL << SQ(file - 1, rank - 1));
    if (file < 7 && rank > 0)
      defenders |= (1ULL << SQ(file + 1, rank - 1));

    if (white_pawns & defenders) {
      score += (PAWN_CHAIN_MG * phase + PAWN_CHAIN_EG * (256 - phase)) / 256;
    }
  }

  BB_FOREACH(sq, black_pawns) {
    int file = SQ_FILE(sq);
    int rank = SQ_RANK(sq);

    Bitboard defenders = 0;
    if (file > 0 && rank < 7)
      defenders |= (1ULL << SQ(file - 1, rank + 1));
    if (file < 7 && rank < 7)
      defenders |= (1ULL << SQ(file + 1, rank + 1));

    if (black_pawns & defenders) {
      score -= (PAWN_CHAIN_MG * phase + PAWN_CHAIN_EG * (256 - phase)) / 256;
    }
  }

  // Hanging pawns - pawns on adjacent files without support, both can advance
  for (int file = 1; file < 7; file++) {
    Bitboard file_mask = FILE_A << file;
    Bitboard adj_right = FILE_A << (file + 1);

    // White hanging pawns on c+d or d+e files (typical)
    if ((file == 2 || file == 3) && (white_pawns & file_mask) &&
        (white_pawns & adj_right)) {
      // Check no pawns on outer adjacent files
      Bitboard outer_files = (file > 1 ? (FILE_A << (file - 2)) : 0) |
                             (file < 6 ? (FILE_A << (file + 2)) : 0);
      if (!(white_pawns & outer_files)) {
        score -=
            (HANGING_PAWN_MG * phase + HANGING_PAWN_EG * (256 - phase)) / 256;
      }
    }

    // Black hanging pawns
    if ((file == 2 || file == 3) && (black_pawns & file_mask) &&
        (black_pawns & adj_right)) {
      Bitboard outer_files = (file > 1 ? (FILE_A << (file - 2)) : 0) |
                             (file < 6 ? (FILE_A << (file + 2)) : 0);
      if (!(black_pawns & outer_files)) {
        score +=
            (HANGING_PAWN_MG * phase + HANGING_PAWN_EG * (256 - phase)) / 256;
      }
    }
  }

  // Pawn islands penalty
  int white_islands = count_pawn_islands(white_pawns);
  int black_islands = count_pawn_islands(black_pawns);

  // More than 2 islands is a weakness
  if (white_islands > 2) {
    int penalty = (white_islands - 2) * PAWN_ISLAND_MG;
    score -= (penalty * phase + penalty * (256 - phase)) / 256;
  }
  if (black_islands > 2) {
    int penalty = (black_islands - 2) * PAWN_ISLAND_MG;
    score += (penalty * phase + penalty * (256 - phase)) / 256;
  }

  // Candidate passed pawns - pawns that could become passed
  BB_FOREACH(sq, white_pawns) {
    int file = SQ_FILE(sq);
    int rank = SQ_RANK(sq);

    // Count enemy pawns that can block
    int blockers = 0;
    for (int r = rank + 1; r < 8; r++) {
      if (black_pawns & (1ULL << SQ(file, r)))
        blockers++;
      if (file > 0 && (black_pawns & (1ULL << SQ(file - 1, r))))
        blockers++;
      if (file < 7 && (black_pawns & (1ULL << SQ(file + 1, r))))
        blockers++;
    }

    // Count own pawns that can support advancement
    int supporters = 0;
    if (file > 0)
      supporters += POPCOUNT(white_pawns & (FILE_A << (file - 1)));
    if (file < 7)
      supporters += POPCOUNT(white_pawns & (FILE_A << (file + 1)));

    // Candidate if supporters >= blockers and not already passed
    if (blockers > 0 && blockers <= 1 && supporters >= blockers) {
      int bonus = (rank - 1) * CANDIDATE_PASSED_MG / 4;
      score += (bonus * phase + bonus * 2 * (256 - phase)) / 256;
    }
  }

  BB_FOREACH(sq, black_pawns) {
    int file = SQ_FILE(sq);
    int rank = SQ_RANK(sq);

    int blockers = 0;
    for (int r = rank - 1; r >= 0; r--) {
      if (white_pawns & (1ULL << SQ(file, r)))
        blockers++;
      if (file > 0 && (white_pawns & (1ULL << SQ(file - 1, r))))
        blockers++;
      if (file < 7 && (white_pawns & (1ULL << SQ(file + 1, r))))
        blockers++;
    }

    int supporters = 0;
    if (file > 0)
      supporters += POPCOUNT(black_pawns & (FILE_A << (file - 1)));
    if (file < 7)
      supporters += POPCOUNT(black_pawns & (FILE_A << (file + 1)));

    if (blockers > 0 && blockers <= 1 && supporters >= blockers) {
      int bonus = (6 - rank) * CANDIDATE_PASSED_MG / 4;
      score -= (bonus * phase + bonus * 2 * (256 - phase)) / 256;
    }
  }

  return score;
}

// Evaluate passed pawns with advanced features
Score evaluate_passed_pawns_advanced(const Position *pos) {
  Score score = 0;
  int phase = phase_eval(pos);

  Bitboard white_pawns = pos->pieces[WHITE][PAWN];
  Bitboard black_pawns = pos->pieces[BLACK][PAWN];

  // Track passed pawn files for outside passed pawn detection
  int white_passed_files[8] = {0};
  int black_passed_files[8] = {0};
  int white_passed_count = 0;
  int black_passed_count = 0;

  int sq;
  BB_FOREACH(sq, white_pawns) {
    int file = SQ_FILE(sq);
    int rank = SQ_RANK(sq);

    // Check if passed
    int passed = 1;
    for (int r = rank + 1; r < 8; r++) {
      if ((black_pawns & (1ULL << SQ(file, r))) ||
          (file > 0 && (black_pawns & (1ULL << SQ(file - 1, r)))) ||
          (file < 7 && (black_pawns & (1ULL << SQ(file + 1, r))))) {
        passed = 0;
        break;
      }
    }

    if (passed) {
      white_passed_files[file] = 1;
      white_passed_count++;

      // Base passed pawn bonus (increasing with rank)
      int bonus_mg = PASSED_PAWN_BASE_MG + (rank - 1) * 8;
      int bonus_eg = PASSED_PAWN_BASE_EG + (rank - 1) * 15;

      // Protected passed pawn bonus
      Bitboard defenders = 0;
      if (file > 0 && rank > 0)
        defenders |= (1ULL << SQ(file - 1, rank - 1));
      if (file < 7 && rank > 0)
        defenders |= (1ULL << SQ(file + 1, rank - 1));

      if (white_pawns & defenders) {
        bonus_mg += PROTECTED_PASSED_MG;
        bonus_eg += PROTECTED_PASSED_EG;
      }

      // King proximity bonus in endgame
      if (phase < 100 && pos->pieces[WHITE][KING] && pos->pieces[BLACK][KING]) {
        int own_king_sq = LSB(pos->pieces[WHITE][KING]);
        int enemy_king_sq = LSB(pos->pieces[BLACK][KING]);

        int own_dist =
            abs(SQ_FILE(own_king_sq) - file) + abs(SQ_RANK(own_king_sq) - 7);
        int enemy_dist = abs(SQ_FILE(enemy_king_sq) - file) +
                         abs(SQ_RANK(enemy_king_sq) - 7);

        bonus_eg += (enemy_dist - own_dist) * 5;
      }

      score += (bonus_mg * phase + bonus_eg * (256 - phase)) / 256;
    }
  }

  BB_FOREACH(sq, black_pawns) {
    int file = SQ_FILE(sq);
    int rank = SQ_RANK(sq);

    int passed = 1;
    for (int r = rank - 1; r >= 0; r--) {
      if ((white_pawns & (1ULL << SQ(file, r))) ||
          (file > 0 && (white_pawns & (1ULL << SQ(file - 1, r)))) ||
          (file < 7 && (white_pawns & (1ULL << SQ(file + 1, r))))) {
        passed = 0;
        break;
      }
    }

    if (passed) {
      black_passed_files[file] = 1;
      black_passed_count++;

      int bonus_mg = PASSED_PAWN_BASE_MG + (6 - rank) * 8;
      int bonus_eg = PASSED_PAWN_BASE_EG + (6 - rank) * 15;

      Bitboard defenders = 0;
      if (file > 0 && rank < 7)
        defenders |= (1ULL << SQ(file - 1, rank + 1));
      if (file < 7 && rank < 7)
        defenders |= (1ULL << SQ(file + 1, rank + 1));

      if (black_pawns & defenders) {
        bonus_mg += PROTECTED_PASSED_MG;
        bonus_eg += PROTECTED_PASSED_EG;
      }

      if (phase < 100 && pos->pieces[WHITE][KING] && pos->pieces[BLACK][KING]) {
        int own_king_sq = LSB(pos->pieces[BLACK][KING]);
        int enemy_king_sq = LSB(pos->pieces[WHITE][KING]);

        int own_dist =
            abs(SQ_FILE(own_king_sq) - file) + abs(SQ_RANK(own_king_sq) - 0);
        int enemy_dist = abs(SQ_FILE(enemy_king_sq) - file) +
                         abs(SQ_RANK(enemy_king_sq) - 0);

        bonus_eg += (enemy_dist - own_dist) * 5;
      }

      score -= (bonus_mg * phase + bonus_eg * (256 - phase)) / 256;
    }
  }

  // Outside passed pawn bonus - passed pawn on wing files while other pawns
  // on opposite side
  if (white_passed_count > 0) {
    // Find leftmost and rightmost pawn files
    int leftmost = 7, rightmost = 0;
    for (int f = 0; f < 8; f++) {
      if ((white_pawns | black_pawns) & (FILE_A << f)) {
        if (f < leftmost)
          leftmost = f;
        if (f > rightmost)
          rightmost = f;
      }
    }

    // Check for outside passed pawn
    for (int f = 0; f < 8; f++) {
      if (white_passed_files[f]) {
        if ((f <= 1 && rightmost >= 5) || (f >= 6 && leftmost <= 2)) {
          score +=
              (OUTSIDE_PASSED_MG * phase + OUTSIDE_PASSED_EG * (256 - phase)) /
              256;
        }
      }
    }
  }

  if (black_passed_count > 0) {
    int leftmost = 7, rightmost = 0;
    for (int f = 0; f < 8; f++) {
      if ((white_pawns | black_pawns) & (FILE_A << f)) {
        if (f < leftmost)
          leftmost = f;
        if (f > rightmost)
          rightmost = f;
      }
    }

    for (int f = 0; f < 8; f++) {
      if (black_passed_files[f]) {
        if ((f <= 1 && rightmost >= 5) || (f >= 6 && leftmost <= 2)) {
          score -=
              (OUTSIDE_PASSED_MG * phase + OUTSIDE_PASSED_EG * (256 - phase)) /
              256;
        }
      }
    }
  }

  return score;
}

// ===== POSITIONAL FEATURES =====

// Evaluate space advantage
Score evaluate_space(const Position *pos) {
  Score score = 0;
  int phase = phase_eval(pos);

  // Space is squares in opponent's half controlled by our pieces
  // Simplified: count pawns and pieces in center/extended center

  Bitboard white_space = 0;
  Bitboard black_space = 0;

  // White controls squares in ranks 4-6
  Bitboard white_territory = RANK_4 | RANK_5 | RANK_6;
  // Black controls squares in ranks 3-5 (from white's perspective)
  Bitboard black_territory = RANK_3 | RANK_4 | RANK_5;

  // Count pieces in enemy territory
  white_space = (pos->pieces[WHITE][PAWN] | pos->pieces[WHITE][KNIGHT] |
                 pos->pieces[WHITE][BISHOP]) &
                white_territory;
  black_space = (pos->pieces[BLACK][PAWN] | pos->pieces[BLACK][KNIGHT] |
                 pos->pieces[BLACK][BISHOP]) &
                black_territory;

  int white_count = POPCOUNT(white_space);
  int black_count = POPCOUNT(black_space);

  int space_diff = (white_count - black_count) * SPACE_BONUS_MG;
  score += (space_diff * phase) / 256;

  return score;
}

// Evaluate center control
Score evaluate_center_control(const Position *pos) {
  Score score = 0;
  int phase = phase_eval(pos);

  // Pawns in center
  int white_center_pawns = POPCOUNT(pos->pieces[WHITE][PAWN] & CENTER_SQUARES);
  int black_center_pawns = POPCOUNT(pos->pieces[BLACK][PAWN] & CENTER_SQUARES);

  // Knights in center/extended center
  int white_center_knights =
      POPCOUNT(pos->pieces[WHITE][KNIGHT] & (CENTER_SQUARES | EXTENDED_CENTER));
  int black_center_knights =
      POPCOUNT(pos->pieces[BLACK][KNIGHT] & (CENTER_SQUARES | EXTENDED_CENTER));

  int center_diff =
      (white_center_pawns - black_center_pawns) * CENTER_CONTROL_MG +
      (white_center_knights - black_center_knights) * (CENTER_CONTROL_MG / 2);

  score += (center_diff * phase) / 256;

  return score;
}

// Evaluate development (mainly for opening)
Score evaluate_development(const Position *pos) {
  Score score = 0;
  int phase = phase_eval(pos);

  // Only relevant in opening
  if (phase < 180)
    return 0;

  // White development
  int white_developed = 0;

  // Knights not on starting squares
  if (!(pos->pieces[WHITE][KNIGHT] & (1ULL << SQ(1, 0))))
    white_developed++;
  if (!(pos->pieces[WHITE][KNIGHT] & (1ULL << SQ(6, 0))))
    white_developed++;

  // Bishops not on starting squares
  if (!(pos->pieces[WHITE][BISHOP] & (1ULL << SQ(2, 0))))
    white_developed++;
  if (!(pos->pieces[WHITE][BISHOP] & (1ULL << SQ(5, 0))))
    white_developed++;

  // King has castled or moved (no longer on e1)
  if (!(pos->pieces[WHITE][KING] & (1ULL << SQ(4, 0))))
    white_developed++;

  // Black development
  int black_developed = 0;

  if (!(pos->pieces[BLACK][KNIGHT] & (1ULL << SQ(1, 7))))
    black_developed++;
  if (!(pos->pieces[BLACK][KNIGHT] & (1ULL << SQ(6, 7))))
    black_developed++;
  if (!(pos->pieces[BLACK][BISHOP] & (1ULL << SQ(2, 7))))
    black_developed++;
  if (!(pos->pieces[BLACK][BISHOP] & (1ULL << SQ(5, 7))))
    black_developed++;
  if (!(pos->pieces[BLACK][KING] & (1ULL << SQ(4, 7))))
    black_developed++;

  int dev_diff = (white_developed - black_developed) * DEVELOPMENT_MG;
  score +=
      (dev_diff * (phase - 180)) / 76; // Scale based on how early in opening

  return score;
}

// (Redundant SEE implementation removed, now using see.c)

// ===== ENDGAME EVALUATION =====

Score evaluate_endgame_knowledge(const Position *pos) {
  Score score = 0;

  // Check for specific endgame patterns
  int material_white = material(pos, WHITE);
  int material_black = material(pos, BLACK);
  int total_material = material_white + material_black;

  // King and Pawn endgame bonuses
  if (total_material < 400) // Very simplified - basically K+P vs K
  {
    // Encourage advancing passed pawns in endgame
    Bitboard white_pawns = pos->pieces[WHITE][PAWN];
    Bitboard black_pawns = pos->pieces[BLACK][PAWN];

    int sq;
    BB_FOREACH(sq, white_pawns) {
      int rank = SQ_RANK(sq);
      // Bonus for advanced pawns
      score += (rank - 1) * 20;
    }

    BB_FOREACH(sq, black_pawns) {
      int rank = SQ_RANK(sq);
      // Bonus for advanced pawns
      score -= (6 - rank) * 20;
    }

    // Bonus for king centralization in K+P endgame
    Bitboard white_king = pos->pieces[WHITE][KING];
    Bitboard black_king = pos->pieces[BLACK][KING];

    if (white_king) {
      int king_sq = LSB(white_king);
      int center_dist = (abs(SQ_FILE(king_sq) - 3) + abs(SQ_RANK(king_sq) - 3));
      score += (8 - center_dist) * 10; // Centralize in endgame
    }

    if (black_king) {
      int king_sq = LSB(black_king);
      int center_dist = (abs(SQ_FILE(king_sq) - 3) + abs(SQ_RANK(king_sq) - 3));
      score -= (8 - center_dist) * 10;
    }
  }

  return score;
}

// ===== DRAW DETECTION AND CONTEMPT =====

// Helper function to check if a side has only light-squared or only
// dark-squared bishops
static int get_bishop_color(const Position *pos, int color) {
  Bitboard bishops = pos->pieces[color][BISHOP];
  if (!bishops)
    return 0; // No bishops

  // Light squares (a1=dark, b1=light, etc.)
  const Bitboard LIGHT_SQUARES = 0x55AA55AA55AA55AAULL;
  const Bitboard DARK_SQUARES = 0xAA55AA55AA55AA55ULL;

  int on_light = POPCOUNT(bishops & LIGHT_SQUARES);
  int on_dark = POPCOUNT(bishops & DARK_SQUARES);

  if (on_light > 0 && on_dark > 0)
    return 3; // Both colors
  if (on_light > 0)
    return 1; // Light only
  if (on_dark > 0)
    return 2; // Dark only
  return 0;
}

// Check if position has insufficient material to mate
DrawType is_insufficient_material(const Position *pos) {
  // Count pieces for each side
  int wp = POPCOUNT(pos->pieces[WHITE][PAWN]);
  int wn = POPCOUNT(pos->pieces[WHITE][KNIGHT]);
  int wb = POPCOUNT(pos->pieces[WHITE][BISHOP]);
  int wr = POPCOUNT(pos->pieces[WHITE][ROOK]);
  int wq = POPCOUNT(pos->pieces[WHITE][QUEEN]);

  int bp = POPCOUNT(pos->pieces[BLACK][PAWN]);
  int bn = POPCOUNT(pos->pieces[BLACK][KNIGHT]);
  int bb = POPCOUNT(pos->pieces[BLACK][BISHOP]);
  int br = POPCOUNT(pos->pieces[BLACK][ROOK]);
  int bq = POPCOUNT(pos->pieces[BLACK][QUEEN]);

  int white_minor = wn + wb;
  int black_minor = bn + bb;

  // If there are pawns, queens, or rooks, it's not insufficient material
  if (wp || bp || wq || bq || wr || br) {
    return DRAW_NONE;
  }

  // K vs K
  if (white_minor == 0 && black_minor == 0) {
    return DRAW_INSUFFICIENT_MATERIAL;
  }

  // K vs K+N or K vs K+B (single minor piece)
  if ((white_minor == 0 && black_minor == 1) ||
      (white_minor == 1 && black_minor == 0)) {
    return DRAW_INSUFFICIENT_MATERIAL;
  }

  // K+N vs K+N
  if (wn == 1 && wb == 0 && bn == 1 && bb == 0) {
    return DRAW_INSUFFICIENT_MATERIAL;
  }

  // K+B vs K+B with same colored bishops
  if (wb == 1 && wn == 0 && bb == 1 && bn == 0) {
    int white_bishop_color = get_bishop_color(pos, WHITE);
    int black_bishop_color = get_bishop_color(pos, BLACK);

    // Same colored bishops = draw
    if (white_bishop_color == black_bishop_color) {
      return DRAW_INSUFFICIENT_MATERIAL;
    }
  }

  // K+N+N vs K (technically can't force mate but not officially insufficient)
  // We'll return NONE here as it's not a clear draw by rule

  return DRAW_NONE;
}

// Detect fortress positions
DrawType is_fortress(const Position *pos) {
  // Count material
  int wp = POPCOUNT(pos->pieces[WHITE][PAWN]);
  int wn = POPCOUNT(pos->pieces[WHITE][KNIGHT]);
  int wb = POPCOUNT(pos->pieces[WHITE][BISHOP]);
  int wr = POPCOUNT(pos->pieces[WHITE][ROOK]);
  int wq = POPCOUNT(pos->pieces[WHITE][QUEEN]);

  int bp = POPCOUNT(pos->pieces[BLACK][PAWN]);
  int bn = POPCOUNT(pos->pieces[BLACK][KNIGHT]);
  int bb = POPCOUNT(pos->pieces[BLACK][BISHOP]);
  int br = POPCOUNT(pos->pieces[BLACK][ROOK]);
  int bq = POPCOUNT(pos->pieces[BLACK][QUEEN]);

  // Total material count (excluding kings)
  int white_mat = wp + wn + wb + wr + wq;
  int black_mat = bp + bn + bb + br + bq;

  // ===== ROOK + PAWN vs ROOK FORTRESS =====
  // R+P vs R: If defending king is in front of the pawn, often a fortress
  if (wr == 1 && wp == 1 && !wn && !wb && !wq && br == 1 && !bp && !bn && !bb &&
      !bq) {
    // White has R+P vs Black R
    Bitboard white_pawn = pos->pieces[WHITE][PAWN];
    Bitboard black_king = pos->pieces[BLACK][KING];

    if (white_pawn && black_king) {
      int pawn_sq = LSB(white_pawn);
      int pawn_file = SQ_FILE(pawn_sq);
      int pawn_rank = SQ_RANK(pawn_sq);
      int bking_sq = LSB(black_king);
      int bking_file = SQ_FILE(bking_sq);
      int bking_rank = SQ_RANK(bking_sq);

      // Check if black king is in front of the pawn
      if (bking_rank > pawn_rank && abs(bking_file - pawn_file) <= 1) {
        // Potential Philidor position / fortress
        // Rook pawn (a or h file) is especially drawish
        if (pawn_file == 0 || pawn_file == 7) {
          // King in the corner = fortress
          if ((pawn_file == 0 && bking_file == 0) ||
              (pawn_file == 7 && bking_file == 7)) {
            return DRAW_FORTRESS;
          }
        }
      }
    }
  }

  // Symmetric for black
  if (br == 1 && bp == 1 && !bn && !bb && !bq && wr == 1 && !wp && !wn && !wb &&
      !wq) {
    Bitboard black_pawn = pos->pieces[BLACK][PAWN];
    Bitboard white_king = pos->pieces[WHITE][KING];

    if (black_pawn && white_king) {
      int pawn_sq = LSB(black_pawn);
      int pawn_file = SQ_FILE(pawn_sq);
      int pawn_rank = SQ_RANK(pawn_sq);
      int wking_sq = LSB(white_king);
      int wking_file = SQ_FILE(wking_sq);
      int wking_rank = SQ_RANK(wking_sq);

      // Check if white king is in front of the pawn (lower rank for black)
      if (wking_rank < pawn_rank && abs(wking_file - pawn_file) <= 1) {
        if (pawn_file == 0 || pawn_file == 7) {
          if ((pawn_file == 0 && wking_file == 0) ||
              (pawn_file == 7 && wking_file == 7)) {
            return DRAW_FORTRESS;
          }
        }
      }
    }
  }

  // ===== BISHOP + WRONG ROOK PAWN =====
  // KB+RP vs K where the rook pawn promotes on wrong color
  if (wb == 1 && wp == 1 && !wn && !wr && !wq && !black_mat) {
    Bitboard white_pawn = pos->pieces[WHITE][PAWN];
    Bitboard black_king = pos->pieces[BLACK][KING];

    if (white_pawn && black_king) {
      int pawn_sq = LSB(white_pawn);
      int pawn_file = SQ_FILE(pawn_sq);

      // Only for rook pawns (a or h file)
      if (pawn_file == 0 || pawn_file == 7) {
        int promotion_sq = (pawn_file == 0) ? 56 : 63; // a8 or h8
        int bishop_color = get_bishop_color(pos, WHITE);

        // Check if promotion square is opposite color of bishop
        // a8 (sq 56) is dark, h8 (sq 63) is light
        int promo_is_light = (promotion_sq == 63) ? 1 : 0;

        int wrong_bishop =
            (bishop_color == 1 &&
             !promo_is_light) ||                   // Light bishop, dark square
            (bishop_color == 2 && promo_is_light); // Dark bishop, light square

        if (wrong_bishop) {
          // Check if black king can reach the corner
          int bking_sq = LSB(black_king);
          int corner_dist =
              (pawn_file == 0)
                  ? abs(SQ_FILE(bking_sq) - 0) + abs(SQ_RANK(bking_sq) - 7)
                  : abs(SQ_FILE(bking_sq) - 7) + abs(SQ_RANK(bking_sq) - 7);

          int pawn_dist = 7 - SQ_RANK(pawn_sq); // Distance to promotion

          // If king can reach the corner before pawn promotes, it's a
          // fortress
          if (corner_dist <= pawn_dist + 1) {
            return DRAW_FORTRESS;
          }
        }
      }
    }
  }

  // Symmetric for black
  if (bb == 1 && bp == 1 && !bn && !br && !bq && !white_mat) {
    Bitboard black_pawn = pos->pieces[BLACK][PAWN];
    Bitboard white_king = pos->pieces[WHITE][KING];

    if (black_pawn && white_king) {
      int pawn_sq = LSB(black_pawn);
      int pawn_file = SQ_FILE(pawn_sq);

      if (pawn_file == 0 || pawn_file == 7) {
        int promotion_sq = (pawn_file == 0) ? 0 : 7; // a1 or h1
        int bishop_color = get_bishop_color(pos, BLACK);

        // a1 (sq 0) is dark, h1 (sq 7) is light
        int promo_is_light = (promotion_sq == 7) ? 1 : 0;

        int wrong_bishop = (bishop_color == 1 && !promo_is_light) ||
                           (bishop_color == 2 && promo_is_light);

        if (wrong_bishop) {
          int wking_sq = LSB(white_king);
          int corner_dist =
              (pawn_file == 0)
                  ? abs(SQ_FILE(wking_sq) - 0) + abs(SQ_RANK(wking_sq) - 0)
                  : abs(SQ_FILE(wking_sq) - 7) + abs(SQ_RANK(wking_sq) - 0);

          int pawn_dist = SQ_RANK(pawn_sq); // Distance to promotion

          if (corner_dist <= pawn_dist + 1) {
            return DRAW_FORTRESS;
          }
        }
      }
    }
  }

  // ===== BLOCKED PAWN POSITIONS =====
  // Detect when all pawns are blocked and neither side can make progress
  if (wp && bp && !wn && !wb && !wr && !wq && !bn && !bb && !br && !bq) {
    // King + Pawns vs King + Pawns endgame
    // Check if pawns are mutually blocked
    Bitboard white_pawns = pos->pieces[WHITE][PAWN];
    Bitboard black_pawns = pos->pieces[BLACK][PAWN];

    int blocked_count = 0;
    int total_pawns = wp + bp;

    // Check each white pawn
    Bitboard temp = white_pawns;
    int sq;
    BB_FOREACH(sq, temp) {
      int forward_sq = sq + 8; // Square in front
      if (forward_sq < 64 && (black_pawns & (1ULL << forward_sq))) {
        blocked_count += 2; // Both pawns are blocked
      }
    }

    // If most pawns are blocked, might be a fortress
    if (blocked_count >= total_pawns - 1 && total_pawns >= 4) {
      // Additional check: neither king can break through
      // This is a simplification; full analysis would be complex
      return DRAW_FORTRESS;
    }
  }

  return DRAW_NONE;
}

// Main function to check for theoretical draws
DrawType is_theoretical_draw(const Position *pos) {
  // Check for 50-move rule
  if (pos->halfmove >= 100) {
    return DRAW_FIFTY_MOVE;
  }

  // Check insufficient material first (faster)
  DrawType draw = is_insufficient_material(pos);
  if (draw != DRAW_NONE) {
    return draw;
  }

  // Check for fortress positions
  draw = is_fortress(pos);
  if (draw != DRAW_NONE) {
    return draw;
  }

  return DRAW_NONE;
}

// ===== CONTEMPT IMPLEMENTATION =====

// Apply contempt factor to evaluation score
Score apply_contempt(Score score, int side_to_move, int contempt) {
  // Contempt is applied from the engine's perspective (typically white)
  // Positive contempt = avoid draws, push for wins
  // Negative contempt = prefer safe draws

  // If score is close to draw (within contempt range), adjust it
  // This makes drawn positions seem worse when we have positive contempt

  if (score == 0) {
    // Exact draw score: apply full contempt penalty
    // From white's perspective, return -contempt (draw is bad if we want to
    // win)
    return (side_to_move == WHITE) ? -contempt : contempt;
  }

  // For near-draw scores, scale contempt based on how close to draw we are
  // This creates a smooth transition rather than a cliff
  if (abs(score) < 100) {
    int contempt_adjustment = contempt * (100 - abs(score)) / 100;

    if (score > 0) {
      // Slightly winning: reduce contempt effect (we're already winning)
      return score - contempt_adjustment / 2;
    } else {
      // Slightly losing: increase contempt effect (fight harder)
      return score - contempt_adjustment;
    }
  }

  // For clear advantages/disadvantages, don't apply contempt
  return score;
}

// Get dynamic contempt based on position characteristics
int get_dynamic_contempt(const Position *pos, int base_contempt) {
  // Calculate material advantage
  int mat_white = 0, mat_black = 0;

  mat_white += POPCOUNT(pos->pieces[WHITE][PAWN]) * VALUE_PAWN;
  mat_white += POPCOUNT(pos->pieces[WHITE][KNIGHT]) * VALUE_KNIGHT;
  mat_white += POPCOUNT(pos->pieces[WHITE][BISHOP]) * VALUE_BISHOP;
  mat_white += POPCOUNT(pos->pieces[WHITE][ROOK]) * VALUE_ROOK;
  mat_white += POPCOUNT(pos->pieces[WHITE][QUEEN]) * VALUE_QUEEN;

  mat_black += POPCOUNT(pos->pieces[BLACK][PAWN]) * VALUE_PAWN;
  mat_black += POPCOUNT(pos->pieces[BLACK][KNIGHT]) * VALUE_KNIGHT;
  mat_black += POPCOUNT(pos->pieces[BLACK][BISHOP]) * VALUE_BISHOP;
  mat_black += POPCOUNT(pos->pieces[BLACK][ROOK]) * VALUE_ROOK;
  mat_black += POPCOUNT(pos->pieces[BLACK][QUEEN]) * VALUE_QUEEN;

  int material_diff = mat_white - mat_black;
  int total_material = mat_white + mat_black;

  // Adjust contempt based on material advantage
  int contempt = base_contempt;

  // If we're significantly ahead, increase contempt (play for win)
  if (material_diff > 200) {
    contempt = base_contempt * 3 / 2; // 50% increase
  } else if (material_diff > 100) {
    contempt = base_contempt * 5 / 4; // 25% increase
  }

  // If we're behind, reduce contempt (more willing to accept draw)
  if (material_diff < -200) {
    contempt = base_contempt / 2; // 50% reduction
  } else if (material_diff < -100) {
    contempt = base_contempt * 3 / 4; // 25% reduction
  }

  // In endgames, contempt matters more (fewer pieces to complicate things)
  if (total_material < 2500) {   // Endgame threshold
    contempt = contempt * 4 / 3; // ~33% increase in endgame
  }

  // Don't let contempt go too high or too low
  if (contempt > 100)
    contempt = 100;
  if (contempt < -50)
    contempt = -50;

  return contempt;
}

// Evaluation with contempt applied
Score evaluate_with_contempt(const Position *pos, int contempt) {
  // First check for theoretical draws
  DrawType draw = is_theoretical_draw(pos);

  if (draw == DRAW_INSUFFICIENT_MATERIAL || draw == DRAW_FORTRESS) {
    // Return contempt-adjusted draw score
    return apply_contempt(0, pos->to_move, contempt);
  }

  if (draw == DRAW_FIFTY_MOVE) {
    // 50-move rule - actual draw
    return 0;
  }

  // Normal evaluation
  Score raw_score = evaluate(pos);

  // Get dynamic contempt based on position
  int dynamic_contempt = get_dynamic_contempt(pos, contempt);

  // Apply contempt to the score
  return apply_contempt(raw_score, pos->to_move, dynamic_contempt);
}
