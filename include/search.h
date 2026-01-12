#ifndef SEARCH_H
#define SEARCH_H

#include "position.h"
#include "tt.h"
#include "types.h"

#define MAX_DEPTH 128
#define HISTORY_MAX 50000

// LMR reduction table
extern int LMR_TABLE[64][64]; // [depth][move_count]

// Tablebase statistics (updated during search)
extern int tb_hits_in_search;

// UCI output options (set from UCI layer)
extern int uci_show_wdl;
extern int uci_chess960;
extern int uci_use_nnue;

// ============================================================================
// PLAYING STYLE CONFIGURATION
// ============================================================================

typedef struct {
  int aggression;      // 0-100: higher = more aggressive play, avoid draws
  int positional;      // 0-100: higher = prefer positional play
  int risk_taking;     // 0-100: higher = willing to sacrifice material
  int draw_acceptance; // 0-100: higher = more willing to accept draws
  int time_pressure;   // 0-100: higher = play faster, simpler positions
} PlayingStyle;

// Default balanced style
#define DEFAULT_STYLE {50, 50, 50, 50, 50}

// ============================================================================
// SEARCH STATISTICS (for performance monitoring)
// ============================================================================

typedef struct {
  // Node counts
  uint64_t nodes_searched;
  uint64_t qnodes;
  int selective_depth;

  // TT statistics
  uint64_t tt_hits;
  uint64_t tt_misses;
  uint64_t tt_collisions;

  // Pruning statistics
  uint64_t null_move_tries;
  uint64_t null_move_cutoffs;
  uint64_t null_move_failures;
  uint64_t lmr_reductions;
  uint64_t lmr_re_searches;
  uint64_t futility_prunes;
  uint64_t rfp_prunes;
  uint64_t lmp_prunes;
  uint64_t see_prunes;
  uint64_t probcut_prunes;
  uint64_t razoring_prunes;

  // Extension statistics
  uint64_t check_extensions;
  uint64_t singular_extensions;
  uint64_t recapture_extensions;
  uint64_t passed_pawn_extensions;

  // Move ordering quality
  uint64_t first_move_cutoffs;
  uint64_t total_cutoffs;
  uint64_t pv_first_move_best;

  // Per-depth statistics
  uint64_t nodes_at_depth[MAX_DEPTH];
  uint64_t cutoffs_at_depth[MAX_DEPTH];

  // Branching factor tracking
  double effective_branching_factor;

  // Aspiration window stats
  uint64_t aspiration_fail_highs;
  uint64_t aspiration_fail_lows;
  uint64_t aspiration_successes;
} SearchStatistics;

// Global statistics (reset per search)
extern SearchStatistics search_stats;

typedef struct {
  Move best_move;
  Score best_score;
  int depth;
  int nodes;
  int qnodes;
  int tt_hits;
  int time_ms;
} SearchInfo;

typedef struct {
  TranspositionTable *tt;
  PawnHashTable *pawn_tt; // Pawn structure hash table
  EvalHashTable *eval_tt; // Static evaluation hash table

  int max_depth;
  int max_time_ms;
  int nodes_limit;

  int nodes;
  int qnodes;
  int tt_hits;
  int start_time_ms;

  Move pv[MAX_DEPTH];
  int pv_length;

  // ===== MOVE ORDERING HEURISTICS =====

  // Killer moves: [ply][0=first, 1=second]
  Move killer_moves[MAX_DEPTH][2];

  // History heuristic: [color][piece][to_square] - quiet moves that caused
  // cutoffs
  int history[2][6][64];

  // Counter move table: [prev_piece][prev_to] -> move that refuted it
  Move counter_moves[6][64];

  // Countermove history: [prev_piece][prev_to][piece][to] - for LMR adjustment
  int countermove_history[6][64][6][64];

  // Capture history: [attacker_piece][to_square][victim_piece] - for capture
  // ordering
  int capture_history[6][64][6];

  // Continuation history: [ply-1 piece][ply-1 to][piece][to] - based on 2-ply
  // sequences We use a simplified version stored per ply
  int (*continuation_history)[6][64]; // Dynamically allocated [6][64] per ply

  // Previous move info for continuation history (piece and to-square)
  int prev_piece[MAX_DEPTH];
  int prev_to[MAX_DEPTH];

  // Followed-by history: tracks what moves follow other moves
  // [piece][to][piece][to] - what moves follow what moves
  int followup_history[6][64][6][64];

  // Position repetition history (Zobrist hashes from root)
  uint64_t repetition_history[MAX_GAME_MOVES];
  int repetition_ply;

  // Last move info for recapture extensions
  Move last_move[MAX_DEPTH];

  // Current ply (for killer moves, etc.)
  int ply;                        // Aspiration window support
  int previous_score;             // Score from previous iteration
  int aspiration_fail_high_count; // Track fail-highs for window adjustment
  int aspiration_fail_low_count;  // Track fail-lows for window adjustment

  // Singular Extensions
  Move excluded_move; // Move to exclude from search (for singular extension
                      // checks)

  // ===== MULTI-PV =====
  int multipv; // Number of PV lines to search

  // ===== CONTEMPT SETTINGS =====
  int contempt; // Base contempt value (centipawns)

  // ===== PLAYING STYLE =====
  PlayingStyle style; // Adaptive playing style parameters

  // Statistics (legacy counters - use search_stats for detailed stats)
  int lmr_reductions;
  int null_cutoffs;
  int futility_prunes;
  int rfp_prunes;     // Reverse futility pruning
  int lmp_prunes;     // Late move pruning
  int see_prunes;     // SEE pruning
  int probcut_prunes; // ProbCut pruning
  int extensions;     // Total extensions

  // ===== SEARCH ITERATION TRACKING =====
  int iterations_completed; // Track iterations for history aging

  // ===== SEARCH STABILITY METRICS =====
  Score iteration_scores[32];     // Track scores from last 32 iterations
  int iteration_count;            // Number of completed iterations
  int score_volatility;           // Measure of score instability
  int last_iteration_instability; // Was last iteration unstable?

  // ===== ASPIRATION WINDOW TUNING =====
  int aspiration_window_size;       // Dynamic window size based on history
  int aspiration_consecutive_fails; // Count of consecutive fails

} SearchState;

// Initialize LMR table
void init_search_tables(void);

SearchState *search_create(int tt_size_mb);
void search_delete(SearchState *search);

Score negamax(SearchState *state, Position *pos, int depth, Score alpha,
              Score beta, SearchInfo *info);

Score quiescence(SearchState *state, Position *pos, Score alpha, Score beta);

Score principal_variation_search(SearchState *state, Position *pos, int depth,
                                 Score alpha, Score beta, SearchInfo *info);

Move iterative_deepening(SearchState *state, Position *pos, int max_time_ms);

// ============================================================================
// SEARCH STATISTICS FUNCTIONS
// ============================================================================

// Reset statistics for new search
void search_stats_reset(void);

// Print detailed statistics (for debugging/optimization)
void search_stats_print(void);

// Calculate effective branching factor
double search_stats_branching_factor(void);

// Get TT hit rate (0.0 - 1.0)
double search_stats_tt_hit_rate(void);

// Get first-move cutoff rate (move ordering quality)
double search_stats_first_move_rate(void);

// ============================================================================
// PLAYING STYLE FUNCTIONS
// ============================================================================

// Set playing style
void search_set_style(SearchState *state, const PlayingStyle *style);

// Get style-adjusted contempt
int search_get_contempt(const SearchState *state);

// Get style-adjusted LMR reduction
int search_get_lmr_reduction(const SearchState *state, int base_reduction);

// Repetition history management
void add_repetition_position(SearchState *state, uint64_t zobrist);
void remove_repetition_position(SearchState *state);

#endif // SEARCH_H
