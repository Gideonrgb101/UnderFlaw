#ifndef EVALUATION_H
#define EVALUATION_H

#include "position.h"
#include "types.h"

// Evaluation weights
typedef struct {
  int opening;
  int endgame;
} EvalWeight;

// ===== DRAW DETECTION =====

// Draw types enumeration
typedef enum {
  DRAW_NONE = 0,              // Not a draw
  DRAW_INSUFFICIENT_MATERIAL, // Neither side can mate (e.g., K vs K, KN vs K)
  DRAW_FORTRESS,              // Blocked position, defending side holds
  DRAW_FIFTY_MOVE,            // 50-move rule
  DRAW_REPETITION,            // Threefold repetition (handled in search)
  DRAW_STALEMATE              // Stalemate (handled in search)
} DrawType;

// Material signature for endgame detection
// Encoded as: white_pieces | (black_pieces << 16)
// Each 16-bit portion: pawns(4) | knights(4) | bishops(4) | rooks(4) |
// queens(4)
typedef uint32_t MaterialKey;

// Insufficient material detection
// Returns DRAW_INSUFFICIENT_MATERIAL if neither side can mate, otherwise
// DRAW_NONE
DrawType is_insufficient_material(const Position *pos);

// Fortress detection
// Detects blocked positions where the defending side cannot lose
DrawType is_fortress(const Position *pos);

// Check if position is a theoretical draw (insufficient material or fortress)
DrawType is_theoretical_draw(const Position *pos);

// ===== CONTEMPT FACTOR =====

// Contempt settings (in centipawns)
// Positive contempt = play for win (avoid draws when ahead)
// Negative contempt = play safe (accept draws easily)
// Zero contempt = neutral evaluation

// Get contempt-adjusted score
// side_to_move: the side whose perspective we're evaluating from
// contempt: contempt value in centipawns (typically 10-50)
// score: raw evaluation score
Score apply_contempt(Score score, int side_to_move, int contempt);

// Get contempt adjustment based on game situation
// Returns appropriate contempt based on material balance, game phase, etc.
int get_dynamic_contempt(const Position *pos, int base_contempt);

// Main evaluation function
Score evaluate(const Position *pos);

// Main evaluation function with contempt
Score evaluate_with_contempt(const Position *pos, int contempt);

// Game phase evaluation (0 = endgame, 256 = opening)
int phase_eval(const Position *pos);

// ===== PIECE-SPECIFIC EVALUATION =====

// Knight outposts evaluation
Score evaluate_knight_outposts(const Position *pos);

// Bishop evaluation (bad bishop, long diagonal)
Score evaluate_bishops(const Position *pos);

// Bishop pair bonus
Score evaluate_bishop_pair(const Position *pos);

// Rook on open/semi-open files
Score evaluate_rook_files(const Position *pos);

// Advanced rook evaluation (7th rank, connected)
Score evaluate_rooks_advanced(const Position *pos);

// Queen evaluation (mobility, tropism, early development)
Score evaluate_queen(const Position *pos);

// ===== PAWN STRUCTURE =====

// Basic pawn structure (doubled, isolated, backward, passed)
Score evaluate_pawns(const Position *pos);

// Advanced pawn structure (chains, hanging, islands, candidates)
Score evaluate_pawns_advanced(const Position *pos);

// Advanced passed pawn evaluation
Score evaluate_passed_pawns_advanced(const Position *pos);

// ===== KING SAFETY =====

// King safety evaluation
Score evaluate_king_safety(const Position *pos);

// ===== POSITIONAL FEATURES =====

// Piece mobility evaluation
Score evaluate_mobility(const Position *pos);

// Space advantage evaluation
Score evaluate_space(const Position *pos);

// Center control evaluation
Score evaluate_center_control(const Position *pos);

// Development evaluation (opening)
Score evaluate_development(const Position *pos);

// ===== ENDGAME =====

// Endgame knowledge evaluation
Score evaluate_endgame_knowledge(const Position *pos);

// ===== STATIC EXCHANGE EVALUATION =====

// Static Exchange Evaluation - estimate capture sequence outcome
Score see(const Position *pos, Move move);

#endif // EVALUATION_H
