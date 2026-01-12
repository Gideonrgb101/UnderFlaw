#include "search.h"
#include "cache.h"
#include "evaluation.h"
#include "movegen.h"
#include "movepicker.h"
#include "position.h"
#include "see.h"
#include "tablebase.h"
#include "threads.h"
#include "types.h"
#include "uci.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// LMR reduction table
int LMR_TABLE[64][64];

// Tablebase hit counter for search statistics
int tb_hits_in_search = 0;

// UCI output options (controlled by UCI layer)
int uci_show_wdl = 0;
int uci_chess960 = 0;

// ============================================================================
// SEARCH STATISTICS
// ============================================================================

// Global search statistics
SearchStatistics search_stats = {0};

// Reset statistics for new search
void search_stats_reset(void) {
  memset(&search_stats, 0, sizeof(search_stats));
}

// Calculate effective branching factor
double search_stats_branching_factor(void) {
  if (search_stats.nodes_at_depth[1] == 0)
    return 0.0;

  // Find the deepest depth with significant nodes
  int deepest = 1;
  for (int d = MAX_DEPTH - 1; d > 1; d--) {
    if (search_stats.nodes_at_depth[d] > 0) {
      deepest = d;
      break;
    }
  }

  if (deepest <= 1)
    return 0.0;

  // EBF = (nodes at deepest) ^ (1/depth)
  double total_nodes = (double)search_stats.nodes_searched;
  return pow(total_nodes, 1.0 / deepest);
}

// Get TT hit rate (0.0 - 1.0)
double search_stats_tt_hit_rate(void) {
  uint64_t total = search_stats.tt_hits + search_stats.tt_misses;
  if (total == 0)
    return 0.0;
  return (double)search_stats.tt_hits / total;
}

// Get first-move cutoff rate (move ordering quality)
double search_stats_first_move_rate(void) {
  if (search_stats.total_cutoffs == 0)
    return 0.0;
  return (double)search_stats.first_move_cutoffs / search_stats.total_cutoffs;
}

// Print detailed statistics (for debugging/optimization)
void search_stats_print(void) {
  printf("info string === Search Statistics ===\n");
  printf("info string Nodes: %llu (Q: %llu, %.1f%%)\n",
         (unsigned long long)search_stats.nodes_searched,
         (unsigned long long)search_stats.qnodes,
         search_stats.nodes_searched > 0
             ? 100.0 * search_stats.qnodes / search_stats.nodes_searched
             : 0.0);
  printf("info string Selective depth: %d\n", search_stats.selective_depth);
  printf("info string Branching factor: %.2f\n",
         search_stats_branching_factor());

  printf("info string TT hit rate: %.1f%% (hits: %llu, misses: %llu)\n",
         search_stats_tt_hit_rate() * 100.0,
         (unsigned long long)search_stats.tt_hits,
         (unsigned long long)search_stats.tt_misses);

  printf("info string First move cutoffs: %.1f%% (%llu / %llu)\n",
         search_stats_first_move_rate() * 100.0,
         (unsigned long long)search_stats.first_move_cutoffs,
         (unsigned long long)search_stats.total_cutoffs);

  printf("info string Null move: %llu tries, %llu cutoffs (%.1f%%)\n",
         (unsigned long long)search_stats.null_move_tries,
         (unsigned long long)search_stats.null_move_cutoffs,
         search_stats.null_move_tries > 0
             ? 100.0 * search_stats.null_move_cutoffs /
                   search_stats.null_move_tries
             : 0.0);

  printf("info string LMR: %llu reductions, %llu re-searches (%.1f%% re-search "
         "rate)\n",
         (unsigned long long)search_stats.lmr_reductions,
         (unsigned long long)search_stats.lmr_re_searches,
         search_stats.lmr_reductions > 0
             ? 100.0 * search_stats.lmr_re_searches /
                   search_stats.lmr_reductions
             : 0.0);

  printf("info string Pruning: futility=%llu, rfp=%llu, lmp=%llu, see=%llu, "
         "probcut=%llu\n",
         (unsigned long long)search_stats.futility_prunes,
         (unsigned long long)search_stats.rfp_prunes,
         (unsigned long long)search_stats.lmp_prunes,
         (unsigned long long)search_stats.see_prunes,
         (unsigned long long)search_stats.probcut_prunes);

  printf("info string Extensions: check=%llu, singular=%llu, recap=%llu, "
         "passed=%llu\n",
         (unsigned long long)search_stats.check_extensions,
         (unsigned long long)search_stats.singular_extensions,
         (unsigned long long)search_stats.recapture_extensions,
         (unsigned long long)search_stats.passed_pawn_extensions);

  printf(
      "info string Aspiration: success=%llu, fail_high=%llu, fail_low=%llu\n",
      (unsigned long long)search_stats.aspiration_successes,
      (unsigned long long)search_stats.aspiration_fail_highs,
      (unsigned long long)search_stats.aspiration_fail_lows);

  fflush(stdout);
}

// ============================================================================
// PLAYING STYLE FUNCTIONS
// ============================================================================

// Set playing style
void search_set_style(SearchState *state, const PlayingStyle *style) {
  if (state && style) {
    state->style = *style;

    // Adjust contempt based on aggression and draw_acceptance
    // Higher aggression = more negative contempt (avoid draws)
    // Higher draw_acceptance = more positive contempt (accept draws)
    int style_contempt =
        (state->style.draw_acceptance - state->style.aggression) / 2;
    state->contempt += style_contempt;
  }
}

// Get style-adjusted contempt
int search_get_contempt(const SearchState *state) {
  if (!state)
    return 0;

  int contempt = state->contempt;

  // Aggressive style: increase contempt magnitude (more negative to avoid
  // draws)
  if (state->style.aggression > 70) {
    contempt -= (state->style.aggression - 70) / 2; // Up to -15 extra
  }

  // High draw acceptance: reduce contempt (more willing to draw)
  if (state->style.draw_acceptance > 70) {
    contempt += (state->style.draw_acceptance - 70) / 3; // Up to +10
  }

  return contempt;
}

// Get style-adjusted LMR reduction
int search_get_lmr_reduction(const SearchState *state, int base_reduction) {
  if (!state || base_reduction <= 0)
    return base_reduction;

  int reduction = base_reduction;

  // Aggressive style: reduce LMR to search deeper
  if (state->style.aggression > 70) {
    reduction -= 1;
  }

  // Time pressure style: increase LMR to search faster
  if (state->style.time_pressure > 70) {
    reduction += 1;
  }

  // Positional style: search more carefully in quiet positions
  if (state->style.positional > 70) {
    reduction -= 1;
  }

  return reduction > 0 ? reduction : 0;
}

// Redundant move_to_str removed, using move_to_string from uci.h

// Extract PV from transposition table
static int extract_pv(SearchState *state, Position *pos, Move *pv,
                      int max_length) {
  int length = 0;
  Position temp = *pos;

  while (length < max_length) {
    Move tt_move = tt_get_best_move(state->tt, temp.zobrist);
    if (tt_move == MOVE_NONE)
      break;

    // Verify the move is legal
    if (!movegen_is_legal(&temp, tt_move))
      break;

    pv[length++] = tt_move;

    // Make the move on temp position
    UndoInfo undo;
    position_make_move(&temp, tt_move, &undo);

    // Check for repetition (avoid infinite loops)
    int repeated = 0;
    for (int i = 0; i < length - 1; i++) {
      Position check = *pos;
      for (int j = 0; j <= i; j++) {
        UndoInfo u;
        position_make_move(&check, pv[j], &u);
      }
      if (check.zobrist == temp.zobrist) {
        repeated = 1;
        break;
      }
    }
    if (repeated)
      break;
  }

  return length;
}

// Initialize LMR table with logarithmic formula
void init_search_tables(void) {
  for (int depth = 0; depth < 64; depth++) {
    for (int moves = 0; moves < 64; moves++) {
      if (depth == 0 || moves == 0) {
        LMR_TABLE[depth][moves] = 0;
      } else {
        // Standard LMR formula
        LMR_TABLE[depth][moves] = (int)(0.5 + log(depth) * log(moves) / 2.0);
        if (LMR_TABLE[depth][moves] < 0)
          LMR_TABLE[depth][moves] = 0;
        if (LMR_TABLE[depth][moves] > depth - 1)
          LMR_TABLE[depth][moves] = depth - 1;
      }
    }
  }
}

// Forward declarations
static Score alpha_beta(SearchState *state, Position *pos, int depth,
                        Score alpha, Score beta);

// ===== MOVE ORDERING HELPERS =====

static void store_killer_move(SearchState *state, int ply, Move move) {
  if (state->killer_moves[ply][0] != move) {
    state->killer_moves[ply][1] = state->killer_moves[ply][0];
    state->killer_moves[ply][0] = move;
  }
}

// Update history with gravity (aging) to prevent overflow
static void update_history(SearchState *state, int color, int piece, int to_sq,
                           int depth) {
  int bonus = depth * depth;
  int *entry = &state->history[color][piece][to_sq];

  // Gravity formula: new = old + bonus - old * |bonus| / max
  // This prevents overflow while maintaining relative ordering
  *entry += bonus - (*entry) * abs(bonus) / HISTORY_MAX;

  // Clamp to bounds
  if (*entry > HISTORY_MAX)
    *entry = HISTORY_MAX;
  if (*entry < -HISTORY_MAX)
    *entry = -HISTORY_MAX;
}

// Penalize history for moves that didn't cause cutoff
static void penalize_history(SearchState *state, int color, int piece,
                             int to_sq, int depth) {
  int penalty = depth * depth;
  int *entry = &state->history[color][piece][to_sq];

  *entry -= penalty - (*entry) * abs(penalty) / HISTORY_MAX;

  if (*entry > HISTORY_MAX)
    *entry = HISTORY_MAX;
  if (*entry < -HISTORY_MAX)
    *entry = -HISTORY_MAX;
}

// Update countermove table - store move that refuted opponent's last move
static void update_countermove(SearchState *state, int prev_piece, int prev_to,
                               Move move) {
  if (prev_piece >= 0 && prev_piece < 6 && prev_to >= 0 && prev_to < 64) {
    state->counter_moves[prev_piece][prev_to] = move;
  }
}

// Get countermove for given previous move
static Move get_countermove(SearchState *state, int prev_piece, int prev_to) {
  if (prev_piece >= 0 && prev_piece < 6 && prev_to >= 0 && prev_to < 64) {
    return state->counter_moves[prev_piece][prev_to];
  }
  return MOVE_NONE;
}

// Update countermove history (for LMR decisions)
static void update_countermove_history(SearchState *state, int prev_piece,
                                       int prev_to, int piece, int to_sq,
                                       int depth) {
  if (prev_piece >= 0 && prev_piece < 6 && prev_to >= 0 && prev_to < 64 &&
      piece >= 0 && piece < 6 && to_sq >= 0 && to_sq < 64) {
    int bonus = depth * depth;
    int *entry = &state->countermove_history[prev_piece][prev_to][piece][to_sq];
    *entry += bonus - (*entry) * abs(bonus) / HISTORY_MAX;
    if (*entry > HISTORY_MAX)
      *entry = HISTORY_MAX;
    if (*entry < -HISTORY_MAX)
      *entry = -HISTORY_MAX;
  }
}

// Update capture history - for ordering captures
static void update_capture_history(SearchState *state, int attacker, int to_sq,
                                   int victim, int depth) {
  if (attacker >= 0 && attacker < 6 && to_sq >= 0 && to_sq < 64 &&
      victim >= 0 && victim < 6) {
    int bonus = depth * depth;
    int *entry = &state->capture_history[attacker][to_sq][victim];
    *entry += bonus - (*entry) * abs(bonus) / HISTORY_MAX;
    if (*entry > HISTORY_MAX)
      *entry = HISTORY_MAX;
    if (*entry < -HISTORY_MAX)
      *entry = -HISTORY_MAX;
  }
}

static int get_capture_history(SearchState *state, int attacker, int to_sq,
                               int victim) {
  if (attacker >= 0 && attacker < 6 && to_sq >= 0 && to_sq < 64 &&
      victim >= 0 && victim < 6) {
    return state->capture_history[attacker][to_sq][victim];
  }
  return 0;
}

static void update_followup_history(SearchState *state, int prev_piece,
                                    int prev_to, int piece, int to_sq,
                                    int depth) {
  if (prev_piece >= 0 && prev_piece < 6 && prev_to >= 0 && prev_to < 64 &&
      piece >= 0 && piece < 6 && to_sq >= 0 && to_sq < 64) {
    int bonus = depth * depth;
    int *entry = &state->followup_history[prev_piece][prev_to][piece][to_sq];
    *entry += bonus - (*entry) * abs(bonus) / HISTORY_MAX;
    if (*entry > HISTORY_MAX)
      *entry = HISTORY_MAX;
    if (*entry < -HISTORY_MAX)
      *entry = -HISTORY_MAX;
  }
}

static int get_followup_history(SearchState *state, int prev_piece, int prev_to,
                                int piece, int to_sq) {
  if (prev_piece >= 0 && prev_piece < 6 && prev_to >= 0 && prev_to < 64 &&
      piece >= 0 && piece < 6 && to_sq >= 0 && to_sq < 64) {
    return state->followup_history[prev_piece][prev_to][piece][to_sq];
  }
  return 0;
}

// ===== REPETITION DETECTION =====

void add_repetition_position(SearchState *state, uint64_t zobrist) {
  if (state->repetition_ply < MAX_GAME_MOVES) {
    state->repetition_history[state->repetition_ply++] = zobrist;
  }
}

void remove_repetition_position(SearchState *state) {
  if (state->repetition_ply > 0) {
    state->repetition_ply--;
  }
}

static int check_repetition(SearchState *state, uint64_t zobrist) {
  // Check backwards through history
  // Scan all positions. Zobrist hash includes side-to-move, so no false
  // positives. Start from TWO positions ago, because the latest position
  // in history IS the current position.

  if (state->repetition_ply < 2)
    return 0;

  for (int i = state->repetition_ply - 2; i >= 0; i--) {
    if (state->repetition_history[i] == zobrist) {
      // Return true on first repetition (avoid cycles)
      return 1;
    }
  }
  return 0;
}

// Decay capture history separately - captures are more position-specific
static void decay_capture_history(SearchState *state) {
  for (int p1 = 0; p1 < 6; p1++) {
    for (int sq = 0; sq < 64; sq++) {
      for (int p2 = 0; p2 < 6; p2++) {
        // More aggressive decay for captures (they age faster)
        state->capture_history[p1][sq][p2] =
            (state->capture_history[p1][sq][p2] * 3) / 5;
      }
    }
  }
}

// Decay history tables between iterations to keep recent moves valued
static void decay_history(SearchState *state) {
  // Reduce all history values by ~20% each iteration
  for (int c = 0; c < 2; c++) {
    for (int p = 0; p < 6; p++) {
      for (int sq = 0; sq < 64; sq++) {
        state->history[c][p][sq] = (state->history[c][p][sq] * 4) / 5;
      }
    }
  }

  // Decay countermove history
  for (int p1 = 0; p1 < 6; p1++) {
    for (int sq1 = 0; sq1 < 64; sq1++) {
      for (int p2 = 0; p2 < 6; p2++) {
        for (int sq2 = 0; sq2 < 64; sq2++) {
          state->countermove_history[p1][sq1][p2][sq2] =
              (state->countermove_history[p1][sq1][p2][sq2] * 4) / 5;
        }
      }
    }
  } // Decay capture history
  decay_capture_history(state);
}

// ===== SEARCH STABILITY AND VOLATILITY =====

// Calculate score volatility based on iteration history
static void update_search_stability(SearchState *state, Score iteration_score) {
  // Store the new iteration score
  if (state->iteration_count < 32) {
    state->iteration_scores[state->iteration_count] = iteration_score;
    state->iteration_count++;
  } else {
    // Shift scores and add new one
    for (int i = 0; i < 31; i++) {
      state->iteration_scores[i] = state->iteration_scores[i + 1];
    }
    state->iteration_scores[31] = iteration_score;
  }

  // Calculate volatility based on score swings
  if (state->iteration_count >= 2) {
    int max_swing = 0;
    for (int i = 1; i < state->iteration_count; i++) {
      int swing =
          abs(state->iteration_scores[i] - state->iteration_scores[i - 1]);
      if (swing > max_swing)
        max_swing = swing;
    }
    state->score_volatility = max_swing;

    // If score swung a lot, mark as unstable
    if (max_swing > 100) {
      state->last_iteration_instability = 1;
    } else {
      state->last_iteration_instability = 0;
    }
  }
}

// ===== ASPIRATION WINDOWS =====

static void get_aspiration_window(SearchState *state, int depth, Score *alpha,
                                  Score *beta) {
  if (depth <= 4) {
    // No aspiration windows in shallow searches
    *alpha = -SCORE_INFINITE;
    *beta = SCORE_INFINITE;
    return;
  } // Use previous score as centerpoint
  Score center = state->previous_score;

  // Dynamic window size based on previous score and search stability
  int delta = 25; // Start with small window

  // Adjust based on search stability (volatility)
  if (state->score_volatility > 0) {
    // More volatile searches need wider windows
    delta += (state->score_volatility / 10);
  }

  // Widen for volatile positions (large scores)
  if (abs(center) > 200)
    delta = abs(center) / 8;
  if (abs(center) > 500)
    delta = 100;

  // Widen based on previous failures - but with decay
  // Consecutive fails widen window aggressively
  if (state->aspiration_consecutive_fails > 0) {
    // First fail: +50, second fail: +100, third fail: use infinite
    delta += 50 * state->aspiration_consecutive_fails;
  }

  // Also account for old-style counters for compatibility
  if (state->aspiration_fail_high_count > 0)
    delta += 25 * state->aspiration_fail_high_count;
  if (state->aspiration_fail_low_count > 0)
    delta += 25 * state->aspiration_fail_low_count;

  // Cap delta but allow for wide windows in uncertain positions
  if (state->aspiration_consecutive_fails >= 2)
    delta = 1000; // Very wide window on repeated failures
  else if (delta > 400)
    delta = 400;

  *alpha = center - delta;
  *beta = center + delta;

  // Clamp to valid range
  if (*alpha < -SCORE_INFINITE)
    *alpha = -SCORE_INFINITE;
  if (*beta > SCORE_INFINITE)
    *beta = SCORE_INFINITE;
}

SearchState *search_create(int tt_size_mb) {
  // Initialize LMR table on first call
  static int tables_initialized = 0;
  if (!tables_initialized) {
    init_search_tables();
    tables_initialized = 1;
  }

  SearchState *state = (SearchState *)malloc(sizeof(SearchState));
  if (!state)
    return NULL;

  state->tt = tt_create(tt_size_mb);
  if (!state->tt) {
    free(state);
    return NULL;
  }

  // Create pawn hash table (1MB default)
  state->pawn_tt = pawn_tt_create(1024);
  if (!state->pawn_tt) {
    tt_delete(state->tt);
    free(state);
    return NULL;
  }

  // Create eval hash table (2MB default)
  state->eval_tt = eval_tt_create(2048);
  if (!state->eval_tt) {
    pawn_tt_delete(state->pawn_tt);
    tt_delete(state->tt);
    free(state);
    return NULL;
  }

  state->max_depth = 32;
  state->max_time_ms = 5000;
  state->nodes_limit = 0;
  state->nodes = 0;
  state->qnodes = 0;
  state->tt_hits = 0;
  state->pv_length = 0;
  state->ply = 0;

  // Initialize killer moves
  memset(state->killer_moves, 0, sizeof(state->killer_moves));

  // Initialize history heuristic
  memset(state->history, 0, sizeof(state->history));

  // Initialize counter moves
  memset(state->counter_moves, 0, sizeof(state->counter_moves));
  // Initialize countermove history
  memset(state->countermove_history, 0, sizeof(state->countermove_history));

  // Initialize capture history
  memset(state->capture_history, 0, sizeof(state->capture_history));

  // Initialize previous move tracking for continuation history
  memset(state->prev_piece, -1, sizeof(state->prev_piece));
  memset(state->prev_to, -1, sizeof(state->prev_to));

  // Initialize followup history
  memset(state->followup_history, 0, sizeof(state->followup_history));

  // Initialize repetition history
  memset(state->repetition_history, 0, sizeof(state->repetition_history));
  state->repetition_ply = 0;

  // Initialize last move tracking
  memset(state->last_move, 0,
         sizeof(state->last_move)); // Initialize aspiration window
  state->previous_score = 0;
  state->aspiration_fail_high_count = 0;
  state->aspiration_fail_low_count =
      0; // Initialize contempt (default: 20 centipawns)
  state->contempt = 20;

  // Initialize playing style (balanced defaults)
  state->style.aggression = 50;
  state->style.positional = 50;
  state->style.risk_taking = 50;
  state->style.draw_acceptance = 50;
  state->style.time_pressure = 50;

  // Initialize statistics
  state->lmr_reductions = 0;
  state->null_cutoffs = 0;
  state->futility_prunes = 0;
  state->rfp_prunes = 0;
  state->lmp_prunes = 0;
  state->see_prunes = 0;
  state->probcut_prunes = 0;
  state->extensions = 0; // Initialize iteration counter
  state->iterations_completed = 0;

  // Initialize search stability tracking
  memset(state->iteration_scores, 0, sizeof(state->iteration_scores));
  state->iteration_count = 0;
  state->score_volatility = 0;
  state->last_iteration_instability = 0;

  // Initialize aspiration window tuning
  state->aspiration_window_size = 25;
  state->aspiration_consecutive_fails = 0;

  return state;
}

void search_delete(SearchState *state) {
  if (state) {
    if (state->tt) {
      tt_delete(state->tt);
    }
    if (state->pawn_tt) {
      pawn_tt_delete(state->pawn_tt);
    }
    if (state->eval_tt) {
      eval_tt_delete(state->eval_tt);
    }
    free(state);
  }
}

static int is_time_up(SearchState *state) {
  // Check thread pool stop signal (for SMP)
  if (threads_should_stop())
    return 1;

  if (state->max_time_ms <= 0)
    return 0;
  if (state->nodes_limit > 0 && state->nodes >= state->nodes_limit)
    return 1;

  // Don't check time until we've searched at least some nodes
  if (state->nodes < 100)
    return 0;

  // Check time periodically (every 4096 nodes)
  if ((state->nodes & 0xFFF) == 0) {
    int current = get_time_ms();
    int elapsed = current - state->start_time_ms;

    // Hard limit: always stop at max_time_ms
    if (elapsed >= state->max_time_ms) {
      return 1;
    }

    // Soft limit: stop at 80% of max time for soft time boundary
    // This allows completing the current iteration with margin
    if (elapsed >= state->max_time_ms * 4 / 5) {
      // Only force stop if we're deep in qsearch or at leaf nodes
      // Otherwise give current iteration a chance to finish
      if (state->ply > 20)
        return 1;
    }
  }

  return 0;
}

static Score evaluate_position(const Position *pos) { return evaluate(pos); }

// Get LMR reduction from table
static int get_lmr_reduction(SearchState *state, Position *pos, int depth,
                             int move_count, int is_pv, int is_capture,
                             int gives_check, Move move) {
  if (depth < 3 || move_count < 4)
    return 0;

  int reduction =
      LMR_TABLE[depth < 64 ? depth : 63][move_count < 64 ? move_count : 63];

  // Reduce less in PV nodes
  if (is_pv && reduction > 0)
    reduction--;

  // Don't reduce captures or checking moves as much
  if (is_capture && reduction > 0)
    reduction--;
  if (gives_check && reduction > 0)
    reduction--;

  // Dynamic reduction based on history score and move characteristics
  int to = MOVE_TO(move);
  int from = MOVE_FROM(move);
  int color = pos->to_move;
  int moving_piece = -1;
  for (int piece = 0; piece < 6; piece++) {
    if (pos->pieces[color][piece] & (1ULL << from)) {
      moving_piece = piece;
      break;
    }
  }

  if (moving_piece >= 0) {
    int history_score = state->history[color][moving_piece][to];

    // Main history adjustment
    if (history_score > 1000 && reduction > 0)
      reduction -= 2;
    else if (history_score > 500 && reduction > 0)
      reduction--;
    else if (history_score < -500)
      reduction += 2;
    else if (history_score < -200)
      reduction++;

    // Continuation history adjustment (moves that follow others)
    if (depth >= 5) {
      int cmh_score = 0;
      if (state->ply > 0) {
        int prev_piece = state->prev_piece[state->ply - 1];
        int prev_to = state->prev_to[state->ply - 1];
        if (prev_piece >= 0 && prev_piece < 6 && prev_to >= 0 && prev_to < 64)
          cmh_score =
              state->countermove_history[prev_piece][prev_to][moving_piece][to];
      }

      if (cmh_score > 800 && reduction > 0)
        reduction--;
      if (cmh_score < -400)
        reduction++;
    }
  }

  // Ensure we don't reduce to qsearch
  if (reduction >= depth - 1)
    reduction = depth - 2;
  if (reduction < 0)
    reduction = 0;

  return reduction;
}

// Helper function for phase-aware futility pruning margin
static int get_futility_margin(SearchState *state, Position *pos, int depth) {
  // Base margin grows with depth
  int margin = 100 + 150 * depth;

  // Endgame positions are more volatile, use wider margin
  int phase = phase_eval(pos);
  if (phase < 64) // Endgame
  {
    margin = (margin * 120) / 100; // +20% wider
  }

  // Opening positions are more positional, use tighter margin
  if (phase > 200) // Opening
  {
    margin = (margin * 80) / 100; // -20% tighter
  }

  return margin;
}

// ===== MOVE ORDERING =====

// Score a move for move ordering purposes
// Higher score = better move (should be searched first)
static int score_move_for_ordering(SearchState *state, Position *pos, Move move,
                                   int ply) {
  int from = MOVE_FROM(move);
  int to = MOVE_TO(move);
  int color = pos->to_move;

  // Find moving piece
  int moving_piece = -1;
  for (int piece = 0; piece < 6; piece++) {
    if (pos->pieces[color][piece] & (1ULL << from)) {
      moving_piece = piece;
      break;
    }
  }

  // TT move (best move from previous search) - highest priority
  Move tt_move = tt_get_best_move(state->tt, pos->zobrist);
  if (move == tt_move)
    return 1000000; // Very high score

  // Captures - order by SEE value, MVV/LVA, and capture history
  if (MOVE_IS_CAPTURE(move)) {
    // Find victim piece
    int victim_piece = -1;
    for (int piece = 0; piece < 6; piece++) {
      if (pos->pieces[color ^ 1][piece] & (1ULL << to)) {
        victim_piece = piece;
        break;
      }
    }

    // Get SEE value (positive = winning capture)
    int see_val = see(pos, move);

    // Get capture history bonus
    int cap_hist = 0;
    if (moving_piece >= 0 && victim_piece >= 0) {
      cap_hist = state->capture_history[moving_piece][to][victim_piece];
    }

    // Score: good captures first (winning captures get 500000+)
    if (see_val > 0) {
      return 500000 + see_val + cap_hist / 100; // Winning captures
    } else if (see_val == 0) {
      int victim_value = victim_piece >= 0 ? piece_value(victim_piece) : 0;
      return 400000 + victim_value + cap_hist / 100; // Equal trades
    } else {
      return 100000 + see_val +
             cap_hist / 100; // Losing captures (below quiets with good history)
    }
  }

  // Killer moves - second priority after good captures
  if (move == state->killer_moves[ply][0])
    return 200000;
  if (move == state->killer_moves[ply][1])
    return 190000;

  // Counter move bonus
  if (ply > 0) {
    int prev_piece = state->prev_piece[ply - 1];
    int prev_to = state->prev_to[ply - 1];
    Move counter = MOVE_NONE;
    if (prev_piece >= 0 && prev_piece < 6 && prev_to >= 0 && prev_to < 64)
      counter = state->counter_moves[prev_piece][prev_to];
    if (move == counter)
      return 180000;
  }

  // Quiet moves - sort by history heuristic + countermove history + followup
  // history
  if (moving_piece >= 0) {
    int hist_score = state->history[color][moving_piece][to];

    // Add countermove history bonus (weighted lower than main history)
    if (ply > 0) {
      int prev_piece = state->prev_piece[ply - 1];
      int prev_to = state->prev_to[ply - 1];
      if (prev_piece >= 0 && prev_piece < 6 && prev_to >= 0 && prev_to < 64) {
        int cmh_score =
            state->countermove_history[prev_piece][prev_to][moving_piece][to];
        hist_score += cmh_score / 3; // Reduced weight for CMH
      }
    }

    // Add followup history bonus (moves that follow other moves well)
    if (ply > 0) {
      int prev_piece = state->prev_piece[ply - 1];
      int prev_to = state->prev_to[ply - 1];
      if (prev_piece >= 0 && prev_piece < 6 && prev_to >= 0 && prev_to < 64) {
        int fuh_score =
            state->followup_history[prev_piece][prev_to][moving_piece][to];
        hist_score += fuh_score / 3; // Reduced weight for FUH
      }
    }

    // Defensive bonus for moves that block opponent's threats (simple
    // heuristic) Moving to opponent's last move's destination gets small boost
    if (ply > 0) {
      Move opponent_last = state->last_move[ply - 1];
      if (opponent_last != MOVE_NONE && to == (int)MOVE_TO(opponent_last)) {
        hist_score += 200; // Small bonus for defending attack square
      }
    }

    return hist_score;
  }

  return 0;
}

// Simple move ordering: sort moves based on score
// We'll use a simple bubble sort since move lists are typically < 256 moves
static void order_moves(SearchState *state, Position *pos, MoveList *moves,
                        int ply) {
  // Store scores for each move
  int scores[256];
  for (int i = 0; i < moves->count; i++) {
    scores[i] = score_move_for_ordering(state, pos, moves->moves[i], ply);
  }

  // Simple bubble sort by score (descending)
  for (int i = 0; i < moves->count - 1; i++) {
    for (int j = i + 1; j < moves->count; j++) {
      if (scores[j] > scores[i]) {
        // Swap moves
        Move temp_move = moves->moves[i];
        moves->moves[i] = moves->moves[j];
        moves->moves[j] = temp_move;

        // Swap scores
        int temp_score = scores[i];
        scores[i] = scores[j];
        scores[j] = temp_score;
      }
    }
  }
}

static Score alpha_beta(SearchState *state, Position *pos, int depth,
                        Score alpha, Score beta) {
  int is_pv = (beta - alpha) > 1;

  // Max ply check to prevent stack overflow from check extensions
  if (state->ply >= MAX_DEPTH - 1) {
    return evaluate_position(pos);
  }

  if (is_time_up(state)) {
    return evaluate_position(pos);
  }

  state->nodes++;
  search_stats.nodes_searched++;

  // Track nodes at this depth and selective depth
  if (depth >= 0 && depth < MAX_DEPTH)
    search_stats.nodes_at_depth[depth]++;
  if (state->ply > search_stats.selective_depth)
    search_stats.selective_depth = state->ply;

  // Check for 50-move rule
  if (pos->halfmove >= 100) {
    return apply_contempt(0, pos->to_move, state->contempt);
  }

  // Check for repetition
  if (check_repetition(state, pos->zobrist)) {
    return apply_contempt(0, pos->to_move, state->contempt);
  } // Check for insufficient material or fortress (theoretical draws)
  DrawType draw_type = is_theoretical_draw(pos);
  if (draw_type == DRAW_INSUFFICIENT_MATERIAL || draw_type == DRAW_FORTRESS) {
    return apply_contempt(0, pos->to_move, state->contempt);
  } // ===== TABLEBASE PROBE =====
  // Probe endgame tablebases for positions with few pieces
  if (tb_available() && state->ply > 0) {
    Score tb_score;
    if (tb_probe_in_search(pos, depth, state->ply, &tb_score)) {
      tb_hits_in_search++;

      // For decisive results, we can return immediately
      if (tb_score > SCORE_TB_WIN - 1000 || tb_score < -SCORE_TB_WIN + 1000) {
        // Store in TT for future lookups (without move since TB doesn't provide
        // one here)
        int flag = TT_FLAG_EXACT;
        if (tb_score >= beta)
          flag = TT_FLAG_LOWER;
        else if (tb_score <= alpha)
          flag = TT_FLAG_UPPER;
        tt_store(state->tt, pos->zobrist, tb_score, depth + 6, flag);
        return tb_score;
      }
    }
  } // Transposition table lookup
  Score tt_score = 0;
  int tt_flag = 0;
  Move tt_move = tt_get_best_move(state->tt, pos->zobrist);

  if (tt_lookup(state->tt, pos->zobrist, &tt_score, depth, &tt_flag)) {
    state->tt_hits++;
    search_stats.tt_hits++;
    if (!is_pv) {
      if (tt_flag == TT_FLAG_EXACT)
        return tt_score;
      if (tt_flag == TT_FLAG_LOWER && tt_score >= beta)
        return tt_score;
      if (tt_flag == TT_FLAG_UPPER && tt_score <= alpha)
        return tt_score;
    }
  } else {
    search_stats.tt_misses++;
  }

  // Terminal node
  if (depth <= 0) {
    return quiescence(state, pos, alpha, beta);
  }

  // ===== INTERNAL ITERATIVE DEEPENING (IID) =====
  // When we have no TT move, do a reduced search to find one
  if (depth >= (is_pv ? 6 : 8) && tt_move == MOVE_NONE) {
    int iid_depth = is_pv ? depth - 2 : depth / 2;
    if (iid_depth > 0) {
      alpha_beta(state, pos, iid_depth, alpha, beta);
      tt_move = tt_get_best_move(state->tt, pos->zobrist);
    }
  }

  // Check extension (but limit to avoid explosion)
  int in_check = position_in_check(pos);
  int base_extension = 0;
  if (in_check && depth < 10 && state->ply < MAX_DEPTH / 2) {
    base_extension = 1;
    state->extensions++;
    search_stats.check_extensions++;
  }

  Score static_eval = evaluate_position(pos);

  // ===== REVERSE FUTILITY PRUNING (RFP) =====    // If static eval is way
  // above beta, we can probably cut
  if (!is_pv && !in_check && depth <= 4 && abs(beta) < SCORE_MATE - 100) {
    int rfp_margin = 70 * depth;
    if (static_eval - rfp_margin >= beta) {
      state->rfp_prunes++;
      search_stats.rfp_prunes++;
      return static_eval;
    }
  }

  // Razoring: if static eval is far below alpha, drop into qsearch
  if (!is_pv && !in_check && depth <= 3) {
    int razor_margin = 300 + 100 * depth;
    if (static_eval + razor_margin < alpha) {
      Score razor_score = quiescence(state, pos, alpha - razor_margin,
                                     alpha - razor_margin + 1);
      if (razor_score + razor_margin <= alpha) {
        return razor_score;
      }
    }
  }

  // ===== PROBCUT =====
  // If a shallow search with a wide margin beats beta, cut
  if (!is_pv && !in_check && depth >= 5 && abs(beta) < SCORE_MATE - 100) {
    int prob_margin = 200; // Standard deviation approx ~200cp
    Score prob_beta = beta + prob_margin;
    // Do a shallow qsearch to check
    Score prob_score = quiescence(state, pos, prob_beta - 1, prob_beta);
    if (prob_score >= prob_beta) {
      // Verify with reduced depth search
      Score verify =
          alpha_beta(state, pos, depth - 4, prob_beta - 1, prob_beta);
      if (verify >= prob_beta) {
        state->probcut_prunes++;
        search_stats.probcut_prunes++;
        return verify;
      }
    }
  }

  // Null move pruning with safety measures
  if (!is_pv && depth >= 3 && !in_check && static_eval >= beta) {
    // Disable null move in endgames (too risky - zugzwang positions)
    int our_pieces = POPCOUNT(pos->occupied[pos->to_move] &
                              ~pos->pieces[pos->to_move][PAWN] &
                              ~pos->pieces[pos->to_move][KING]);

    if (our_pieces > 1) // Have at least 2 non-pawn pieces
    {
      search_stats.null_move_tries++;
      Position temp = *pos;

      // Make null move
      temp.to_move ^= 1;
      temp.zobrist ^= zobrist_to_move;
      temp.enpassant = -1;
      if (!position_in_check(&temp)) {
        // Adaptive R based on depth and eval margin
        // Base: 3 + depth/6 (standard formula)
        int R = 3 + depth / 6;

        // Increase R if we're well above beta (safer cutoff)
        int eval_margin = static_eval - beta;
        if (eval_margin > 200)
          R++;
        if (eval_margin > 400)
          R++;

        // Reduce R in endgames (less reliable cutoff signal)
        int phase = phase_eval(pos);
        if (phase < 64) // Endgame territory
          if (R > 3)
            R--;

        // Cap R to avoid excessive reduction
        if (R > depth - 2)
          R = depth - 2;
        if (R < 1)
          R = 1;

        Score null_score =
            -alpha_beta(state, &temp, depth - R - 1, -beta, -beta + 1);

        if (null_score >= beta) {
          state->null_cutoffs++;
          search_stats.null_move_cutoffs++;
          // Verification search at high depths (especially important at depth >
          // 8)
          if (depth > 8) {
            Score verify =
                alpha_beta(state, pos, depth - R - 1, beta - 1, beta);
            if (verify >= beta)
              return beta;
          } else {
            return beta;
          }
        }
      }
    }
  }

  // Futility pruning
  int futility_margin_val = get_futility_margin(state, pos, depth);
  int can_futility = !is_pv && !in_check && depth <= 3 &&
                     static_eval + futility_margin_val <= alpha;

  // ===== SINGULAR EXTENSIONS (Phase 3) =====
  int singular_extension = 0;

  // Only attempt singular extensions at sufficient depth and if we have a
  // valid TT move that bounds the score (lower bound or exact)
  if (state->ply > 0 && depth >= 8 && tt_move != MOVE_NONE &&
      (tt_flag == TT_FLAG_LOWER || tt_flag == TT_FLAG_EXACT) &&
      abs(tt_score) < SCORE_MATE - 1000 && state->excluded_move == MOVE_NONE) {

    int margin = 2 * depth;
    Score singular_beta = tt_score - margin;

    Move saved_excluded = state->excluded_move;
    state->excluded_move = tt_move;

    // Perform reduced depth search (depth - 3 is standard for singular
    // extensions)
    int singular_depth = (depth - 1) / 2; // Actually original code used depth/2
    // Let's use depth - 3 as a more conservative/standard approach at high
    // depths
    if (depth > 10)
      singular_depth = depth - 3;

    Score value = alpha_beta(state, pos, singular_depth, singular_beta - 1,
                             singular_beta);

    state->excluded_move = saved_excluded;

    if (value < singular_beta) {
      singular_extension = 1;
      search_stats.singular_extensions++;
    }
  }

  // Use MovePicker for staged move generation and ordering
  MovePicker mp;
  int prev_p = state->ply > 0 ? state->prev_piece[state->ply - 1] : -1;
  int prev_t = state->ply > 0 ? state->prev_to[state->ply - 1] : 0;
  Move countermove = get_countermove(state, prev_p, prev_t);

  movepicker_init(&mp, pos, tt_move, state->killer_moves[state->ply][0],
                  state->killer_moves[state->ply][1], countermove,
                  state->history[pos->to_move]);

  int legal_moves = 0;
  Score best_score = -SCORE_INFINITE;
  Move best_move = MOVE_NONE;

  // Track quiet moves tried (for history penalty on cutoff)
  Move quiets_tried[64];
  int quiets_tried_count = 0;

  // Track captures tried (for capture history penalty)
  Move captures_tried[32];
  int captures_tried_count = 0;

  Move move;
  while ((move = movepicker_next(&mp)) != MOVE_NONE) {

    int from_sq = MOVE_FROM(move);
    int to_sq = MOVE_TO(move);

    // Find moving piece and validate move source
    int moving_piece = -1;
    for (int piece = 0; piece < 6; piece++) {
      if (pos->pieces[pos->to_move][piece] & (1ULL << from_sq)) {
        moving_piece = piece;
        break;
      }
    }

    // Skip invalid moves (e.g. from TT collision)
    if (moving_piece == -1)
      continue;

    int is_capture = MOVE_IS_CAPTURE(move);
    int is_promotion = MOVE_PROMO(move) > 0;
    int gives_check = 0; // Computed after make_move

    // Futility pruning for quiet moves (don't prune first move)
    if (can_futility && !is_capture && !is_promotion && legal_moves > 0) {
      state->futility_prunes++;
      search_stats.futility_prunes++;
      continue;
    }

    // ===== RAZORING (Phase 3) =====
    // Pruning at low depths if static eval is very bad
    if (!is_pv && !in_check && depth <= 3 && static_eval != -SCORE_INFINITE) {
      int razor_margin = 350 + depth * 150;

      if (static_eval < beta - razor_margin) {
        // If the position is so bad that even a "razor margin" doesn't help,
        // drop directly into simple Quiescence search to confirm it fails
        // low. Note: We use qsearch only if we are truly desperate.

        if (depth <= 1) {
          int val = quiescence(state, pos, alpha, beta);
          if (val > alpha)
            val = quiescence(state, pos, val,
                             beta); // Re-search if it raises alpha

          if (val < beta) {
            search_stats.razoring_prunes++;
            return val;
          }
        }
      }
    }

    // ===== ENHANCED LATE MOVE PRUNING (LMP) =====
    // Skip very late quiet moves at low depths (more aggressive)
    if (!is_pv && !in_check && depth <= 7 && !is_capture && !is_promotion &&
        legal_moves > 0) { // LMP threshold: 3 + 2 * depth^2
      int lmp_threshold = 3 + 2 * depth * depth;
      if (legal_moves > lmp_threshold) {
        state->lmp_prunes++;
        search_stats.lmp_prunes++;
        continue;
      }
    }

    // ===== SEE PRUNING FOR QUIET MOVES =====
    // Skip quiet moves with bad SEE at low depths
    if (!is_pv && depth <= 4 && !in_check && !is_capture && legal_moves > 0) {
      int see_val = see(pos, move);
      if (see_val < -50 * depth) {
        state->see_prunes++;
        search_stats.see_prunes++;
        continue;
      }
    }

    // ===== EXTENSIONS =====
    int extension = base_extension;

    // Singular extension for TT move
    if (move == tt_move && singular_extension) {
      extension = 1;
      state->extensions++;
    }

    // Recapture extension - if we recapture on the same square
    if (state->ply > 0 && is_capture) {
      Move last = state->last_move[state->ply - 1];
      if (last != MOVE_NONE && MOVE_TO(last) == (unsigned)to_sq) {
        // This is a recapture on same square - extend if we're at reasonable
        // depth
        if (extension == 0 && depth < 8) {
          extension = 1;
          state->extensions++;
        }
      }
    }

    // Pawn push to 7th rank extension
    if (moving_piece == PAWN && extension == 0) {
      int to_rank = SQ_RANK(to_sq);
      if ((pos->to_move == WHITE && to_rank == 6) ||
          (pos->to_move == BLACK && to_rank == 1)) {
        extension = 1;
        state->extensions++;
      }
    }

    UndoInfo undo;
    position_make_move(pos, move, &undo);

    // Filter illegal moves (leaves king in check)
    pos->to_move ^= 1;
    int is_illegal = position_in_check(pos);
    pos->to_move ^= 1;

    if (is_illegal) {
      position_unmake_move(pos, move, &undo);
      continue;
    }

    legal_moves++;

    // Prefetch TT entry for the new position (will be used in recursive call)
    tt_prefetch_entry(state->tt, pos->zobrist);

    // Check if move gives check (for LMR adjustment)
    gives_check = position_in_check(pos);

    // Add position to repetition history
    add_repetition_position(state, pos->zobrist);
    state->last_move[state->ply] = move;

    // Store previous move info for continuation history before incrementing
    // ply
    state->prev_piece[state->ply] = moving_piece;
    state->prev_to[state->ply] = to_sq;

    state->ply++;

    // Track moves tried for history penalty
    if (is_capture) {
      if (captures_tried_count < 32)
        captures_tried[captures_tried_count++] = move;
    } else {
      if (quiets_tried_count < 64)
        quiets_tried[quiets_tried_count++] = move;
    }

    Score score;

    if (legal_moves == 1) {
      // First move - full window search
      score = -alpha_beta(state, pos, depth - 1 + extension, -beta, -alpha);
    } else { // Late Move Reductions
      int reduction = 0;
      if (depth >= 3 && legal_moves > 3 && !is_capture && !in_check &&
          !gives_check) {
        reduction = get_lmr_reduction(state, pos, depth, legal_moves, is_pv,
                                      is_capture, gives_check, move);
        state->lmr_reductions++;
        search_stats.lmr_reductions++;
      }

      // Null window search with possible reduction
      score = -alpha_beta(state, pos, depth - 1 + extension - reduction,
                          -alpha - 1, -alpha);

      // Re-search if LMR failed high
      if (reduction > 0 && score > alpha) {
        search_stats.lmr_re_searches++;
        score =
            -alpha_beta(state, pos, depth - 1 + extension, -alpha - 1, -alpha);
      }

      // Full re-search if null window failed high in PV
      if (is_pv && score > alpha && score < beta) {
        score = -alpha_beta(state, pos, depth - 1 + extension, -beta, -alpha);
      }
    }

    state->ply--;
    position_unmake_move(pos, move, &undo);

    // Remove position from repetition history
    remove_repetition_position(state);

    if (is_time_up(state)) {
      return best_score != -SCORE_INFINITE ? best_score
                                           : evaluate_position(pos);
    }

    if (score > best_score) {
      best_score = score;
      best_move = move;

      if (score > alpha) {
        if (abs(score) > 30000 && depth > 0) {
          char mv_str[10];
          move_to_string(move, mv_str);
          char fen[256];
          position_to_fen(pos, fen, 256);
          printf(
              "info string High Score: %d at depth %d from move %s FEN: %s\n",
              score, depth, mv_str, fen);
        }
        alpha = score;
        if (alpha >= beta) {
          // Track cutoff statistics
          search_stats.total_cutoffs++;
          if (legal_moves == 1)
            search_stats.first_move_cutoffs++;
          search_stats
              .cutoffs_at_depth[depth < MAX_DEPTH ? depth : MAX_DEPTH - 1]++;

          // Beta cutoff - update all move ordering heuristics
          int color = pos->to_move;

          if (!is_capture) {
            // Update killer moves
            store_killer_move(state, state->ply, move);

            // Update history for the cutoff move
            update_history(state, color, moving_piece, to_sq, depth);

            // Update countermove table
            if (state->ply > 0) {
              int prev_p = state->prev_piece[state->ply - 1];
              int prev_t = state->prev_to[state->ply - 1];
              update_countermove(state, prev_p, prev_t, move);

              // Update countermove history
              update_countermove_history(state, prev_p, prev_t, moving_piece,
                                         to_sq, depth);

              // Update followup history
              update_followup_history(state, prev_p, prev_t, moving_piece,
                                      to_sq, depth);
            }

            // Penalize quiet moves that didn't cause cutoff
            for (int q = 0; q < quiets_tried_count - 1; q++) {
              Move qm = quiets_tried[q];
              int qfrom = MOVE_FROM(qm);
              int qto = MOVE_TO(qm);
              int qpiece = -1;
              for (int p = 0; p < 6; p++) {
                if (pos->pieces[color][p] & (1ULL << qfrom)) {
                  qpiece = p;
                  break;
                }
              }
              if (qpiece >= 0) {
                penalize_history(state, color, qpiece, qto, depth);
              }
            }
          } else {
            // Update capture history for the cutoff capture
            int victim = -1;
            for (int p = 0; p < 6; p++) {
              if (pos->pieces[color ^ 1][p] & (1ULL << to_sq)) {
                victim = p;
                break;
              }
            }
            if (moving_piece >= 0 && victim >= 0) {
              update_capture_history(state, moving_piece, to_sq, victim, depth);
            }

            // Penalize captures that didn't cause cutoff
            for (int c = 0; c < captures_tried_count - 1; c++) {
              Move cm = captures_tried[c];
              int cfrom = MOVE_FROM(cm);
              int cto = MOVE_TO(cm);
              int cpiece = -1;
              int cvictim = -1;
              for (int p = 0; p < 6; p++) {
                if (pos->pieces[color][p] & (1ULL << cfrom))
                  cpiece = p;
                if (pos->pieces[color ^ 1][p] & (1ULL << cto))
                  cvictim = p;
              }
              if (cpiece >= 0 && cvictim >= 0) {
                // Negative bonus = penalty
                int *entry = &state->capture_history[cpiece][cto][cvictim];
                int penalty = depth * depth;
                *entry -= penalty - (*entry) * abs(penalty) / HISTORY_MAX;
                if (*entry < -HISTORY_MAX)
                  *entry = -HISTORY_MAX;
              }
            }
          }
          break;
        }
      }
    }
  }

  // Check for mate or stalemate
  if (legal_moves == 0) {
    if (in_check) {
      return -SCORE_MATE + state->ply; // Checkmate
    } else {
      return 0; // Stalemate
    }
  }

  // Store in transposition table with best move
  int flag = TT_FLAG_EXACT;
  if (best_score <= alpha)
    flag = TT_FLAG_UPPER;
  else if (best_score >= beta)
    flag = TT_FLAG_LOWER;

  tt_store_with_move(state->tt, pos->zobrist, best_score, best_move, depth,
                     flag);

  return best_score;
}

Score quiescence(SearchState *state, Position *pos, Score alpha, Score beta) {
  if (is_time_up(state)) {
    return evaluate_position(pos);
  }

  state->qnodes++;
  search_stats.qnodes++;

  // Track selective depth in quiescence too
  if (state->ply > search_stats.selective_depth)
    search_stats.selective_depth = state->ply;

  // Transposition table lookup
  Score tt_score;
  int tt_flag;
  Move tt_move = MOVE_NONE;
  if (tt_lookup(state->tt, pos->zobrist, &tt_score, 0, &tt_flag)) {
    state->tt_hits++;
    tt_move = tt_get_best_move(state->tt, pos->zobrist);
    if (tt_flag == TT_FLAG_EXACT)
      return tt_score;
    if (tt_flag == TT_FLAG_LOWER)
      alpha = (alpha > tt_score) ? alpha : tt_score;
    if (tt_flag == TT_FLAG_UPPER)
      beta = (beta < tt_score) ? beta : tt_score;
    if (alpha >= beta)
      return tt_score;
  }

  Score stand_pat = evaluate_position(pos);

  if (stand_pat >= beta) {
    return beta;
  }

  // Delta pruning
  int delta = 900; // Queen value
  if (stand_pat + delta < alpha) {
    return alpha;
  }

  if (stand_pat > alpha) {
    alpha = stand_pat;
  }

  // Use move picker for captures
  MovePicker mp;
  movepicker_init_quiescence(&mp, pos, tt_move);

  Move best_move = MOVE_NONE;
  Move move;
  while ((move = movepicker_next(&mp)) != MOVE_NONE) {
    // SEE pruning - skip bad captures (already filtered by movepicker for
    // good captures)
    int see_val = see_capture(pos, move);
    if (see_val < 0)
      continue;

    UndoInfo undo;
    position_make_move(pos, move, &undo);

    // Prefetch TT entry for quiescence recursion
    tt_prefetch_entry(state->tt, pos->zobrist);

    Score score = -quiescence(state, pos, -beta, -alpha);

    position_unmake_move(pos, move, &undo);

    if (is_time_up(state)) {
      return alpha;
    }

    if (score > alpha) {
      alpha = score;
      best_move = move;
      if (alpha >= beta) {
        tt_store_with_move(state->tt, pos->zobrist, beta, best_move, 0,
                           TT_FLAG_LOWER);
        return beta;
      }
    }
  }

  tt_store_with_move(state->tt, pos->zobrist, alpha, best_move, 0,
                     TT_FLAG_EXACT);
  return alpha;
}

Score negamax(SearchState *state, Position *pos, int depth, Score alpha,
              Score beta, SearchInfo *info) {
  state->nodes = 0;
  state->qnodes = 0;
  state->tt_hits = 0;
  state->start_time_ms = clock() / (CLOCKS_PER_SEC / 1000);

  Score best_score = alpha_beta(state, pos, depth, alpha, beta);

  info->best_score = best_score;
  info->depth = depth;
  info->nodes = state->nodes;
  info->qnodes = state->qnodes;
  info->tt_hits = state->tt_hits;
  info->time_ms = (clock() / (CLOCKS_PER_SEC / 1000)) - state->start_time_ms;

  return best_score;
}

Score principal_variation_search(SearchState *state, Position *pos, int depth,
                                 Score alpha, Score beta, SearchInfo *info) {
  // For now, use negamax - PVS is integrated into alpha_beta
  return negamax(state, pos, depth, alpha, beta, info);
}

// Iterative deepening with best move tracking
Move iterative_deepening(SearchState *state, Position *pos, int max_time_ms) {
  state->max_time_ms = max_time_ms;
  state->start_time_ms = get_time_ms();
  state->nodes = 0;
  state->qnodes = 0;
  state->tt_hits = 0;
  tb_hits_in_search = 0;

  // Reset global search statistics for this search
  search_stats_reset();

  // Increment TT generation for new search
  tt_new_search(state->tt);

  // Generate root moves
  MoveList root_moves;
  movegen_all(pos, &root_moves);

  Move best_move = MOVE_NONE;
  int best_depth = 0;

  // ===== ROOT TABLEBASE PROBE =====
  // If position is in tablebases, use TB move directly
  if (tb_available() && tb_probe_eligible(pos)) {
    TBResult wdl;
    int dtz;
    Move tb_move = tb_probe_root(pos, &wdl, &dtz);

    if (tb_move != MOVE_NONE && wdl != TB_RESULT_UNKNOWN &&
        wdl != TB_RESULT_FAILED) {
      // We have a tablebase move - print info and return it
      Score tb_score = tb_wdl_to_score(wdl, dtz, 0);

      printf("info depth 1 score cp %d tbhits 1 pv ", tb_score);

      // Print TB move
      char move_str[6];
      int from = MOVE_FROM(tb_move);
      int to = MOVE_TO(tb_move);
      int promo = MOVE_PROMO(tb_move);
      move_str[0] = 'a' + SQ_FILE(from);
      move_str[1] = '1' + SQ_RANK(from);
      move_str[2] = 'a' + SQ_FILE(to);
      move_str[3] = '1' + SQ_RANK(to);
      if (promo > 0) {
        const char *promos = "nbrq";
        move_str[4] = promos[promo - 1];
        move_str[5] = '\0';
      } else {
        move_str[4] = '\0';
      }
      printf("%s\n", move_str);
      fflush(stdout);

      return tb_move;
    }
  }

  // Find first legal move as fallback
  for (int i = 0; i < root_moves.count; i++) {
    if (movegen_is_legal(pos, root_moves.moves[i])) {
      best_move = root_moves.moves[i];
      break;
    }
  }
  for (int depth = 1; depth <= state->max_depth; depth++) {
    // Save original position for safety
    Position saved_pos = *pos;

    Move depth_best_move = MOVE_NONE;
    Score depth_best_score = -SCORE_INFINITE;

    // Age history tables between iterations for freshness
    if (depth > 1) {
      decay_history(state);
    }

    // Use aspiration windows at root level
    // MultiPV Loop
    Move excluded_moves[MAX_MULTIPV];
    int excluded_count = 0;

    // Ensure multipv is at least 1
    int multipv_count = state->multipv;
    if (multipv_count < 1)
      multipv_count = 1;
    if (multipv_count > MAX_MULTIPV)
      multipv_count = MAX_MULTIPV;

    for (int pv_idx = 0; pv_idx < multipv_count; pv_idx++) {
      Score root_alpha, root_beta;

      // For first PV logic, use aspiration window if possible
      // For subsequent PVs, we reset filter or just use full window?
      // A simple approach: Use aspiration for 1st, full for others (or track
      // scores per PV line)

      if (pv_idx == 0) {
        get_aspiration_window(state, depth, &root_alpha, &root_beta);
      } else {
        root_alpha = -SCORE_INFINITE;
        root_beta = SCORE_INFINITE;
      }

      int aspiration_attempt = 0;
      int max_aspiration_attempts = 3;
      if (pv_idx > 0)
        max_aspiration_attempts = 1; // Don't aspirate for lower PVs

      while (aspiration_attempt < max_aspiration_attempts) {
        // Reset best score for this aspiration attempt
        depth_best_score = -SCORE_INFINITE;
        depth_best_move = MOVE_NONE;

        // Search all legal root moves to find the best one
        for (int i = 0; i < root_moves.count; i++) {
          Move move = root_moves.moves[i];
          if (!movegen_is_legal(pos, move))
            continue;

          // Check if move is excluded (MultiPV)
          int is_excluded = 0;
          for (int k = 0; k < excluded_count; k++) {
            if (move == excluded_moves[k]) {
              is_excluded = 1;
              break;
            }
          }
          if (is_excluded)
            continue;

          // Save original repetition ply to restore it later
          int saved_repetition_ply = state->repetition_ply;
          state->ply = 0; // Reset ply for each root move

          UndoInfo undo;
          position_make_move(pos, move, &undo);

          // Add new position (after root move) to repetition history
          add_repetition_position(state, pos->zobrist);
          state->ply++; // Increment ply after making root move

          // Search this move with aspiration window
          Score score =
              -alpha_beta(state, pos, depth - 1, -root_beta, -root_alpha);

          state->ply--; // Decrement ply before unmake
          position_unmake_move(pos, move, &undo);

          // Restore repetition history after each root move
          state->repetition_ply = saved_repetition_ply;

          // Track the best move at this depth
          if (score > depth_best_score) {
            depth_best_score = score;
            depth_best_move = move;
          }

          if (is_time_up(state))
            break;
        } // Check if aspiration window failed

        if (is_time_up(state))
          break;

        // Aspiration logic (Only relevant for pv_idx == 0 or if we aspirate
        // others)
        if (depth_best_score <= root_alpha) {
          // Fail low
          state->aspiration_fail_low_count++;
          state->aspiration_consecutive_fails++;
          int delta = root_beta - root_alpha;
          delta *= 2;
          if (delta > 500) {
            root_alpha = -SCORE_INFINITE;
            root_beta = SCORE_INFINITE;
          } else {
            root_alpha = depth_best_score - delta;
            if (root_alpha < -SCORE_INFINITE)
              root_alpha = -SCORE_INFINITE;
          }
          aspiration_attempt++;
        } else if (depth_best_score >= root_beta) {
          // Fail high
          state->aspiration_fail_high_count++;
          state->aspiration_consecutive_fails++;
          int delta = root_beta - root_alpha;
          delta *= 2;
          if (delta > 500) {
            root_alpha = -SCORE_INFINITE;
            root_beta = SCORE_INFINITE;
          } else {
            root_beta = depth_best_score + delta;
            if (root_beta > SCORE_INFINITE)
              root_beta = SCORE_INFINITE;
          }
          aspiration_attempt++;
        } else {
          // Success
          state->aspiration_fail_high_count = 0;
          state->aspiration_fail_low_count = 0;
          state->aspiration_consecutive_fails = 0;
          break;
        }
      }
      if (is_time_up(state) && best_depth > 0) {
        // If time up, we might have partial results for this PV line.
        // Should we break?
        break;
      }

      // Update best move if we found a better one (For Main PV only? Or track
      // local best?) depth_best_move is the best for THIS PV line.
      if (depth_best_move != MOVE_NONE) {
        if (pv_idx == 0) {
          // Main PV updates global "best_move" return value
          best_move = depth_best_move;
          best_depth = depth;
          state->previous_score = depth_best_score;
          update_search_stability(state, depth_best_score);
          // Store in TT for Main PV only? Or all? Storing all might thrash
          // TT? Usually store all.
        }

        tt_store_with_move(state->tt, saved_pos.zobrist, depth_best_score,
                           depth_best_move, depth, TT_FLAG_EXACT);

        // Exclude for next PV
        if (excluded_count < MAX_MULTIPV) {
          excluded_moves[excluded_count++] = depth_best_move;
        }
      } else {
        // No move found? Break (maybe no legal moves left)
        break;
      }

      // Debug output
      int time_elapsed = get_time_ms() - state->start_time_ms;
      if (time_elapsed < 1)
        time_elapsed = 1;
      int nps = (int)(((long long)state->nodes * 1000) / time_elapsed);

      // Extract PV from transposition table
      Move pv[MAX_DEPTH];
      int pv_length = extract_pv(state, &saved_pos, pv, depth);

      // Build PV string
      char pv_str[2048] = "";
      int pv_pos = 0;
      for (int i = 0; i < pv_length && pv_pos < 2000; i++) {
        char move_str[10];
        move_to_string(pv[i], move_str);
        if (i > 0)
          pv_str[pv_pos++] = ' ';
        int len = strlen(move_str);
        memcpy(pv_str + pv_pos, move_str, len);
        pv_pos += len;
      }
      pv_str[pv_pos] = '\0';

      // Calculate hashfull
      int hashfull = tt_hashfull(state->tt);

      // Output info
      printf("info depth %d multipv %d", depth, pv_idx + 1);

      if (depth_best_score > SCORE_MATE - 100) {
        int mate_in = (SCORE_MATE - depth_best_score + 1) / 2;
        printf(" score mate %d", mate_in);
      } else if (depth_best_score < -SCORE_MATE + 100) {
        int mate_in = -(SCORE_MATE + depth_best_score + 1) / 2;
        printf(" score mate %d", mate_in);
      } else {
        printf(" score cp %d", depth_best_score);
        // WDL if enabled and main PV
        if (uci_show_wdl && pv_idx == 0) {
          double a = 0.004;
          double win_p = 1.0 / (1.0 + exp(-a * depth_best_score));
          double loss_p = 1.0 / (1.0 + exp(a * depth_best_score));
          double draw_p = 1.0 - win_p - loss_p;
          if (draw_p < 0)
            draw_p = 0;
          double total = win_p + draw_p + loss_p;
          int win = (int)(win_p / total * 1000 + 0.5);
          int loss = (int)(loss_p / total * 1000 + 0.5);
          int draw = 1000 - win - loss;
          printf(" wdl %d %d %d", win, draw, loss);
        }
      }
      printf(" nodes %d time %d nps %d hashfull %d", state->nodes, time_elapsed,
             nps, hashfull);
      if (tb_hits_in_search > 0)
        printf(" tbhits %d", tb_hits_in_search);
      printf(" pv %s\n", pv_str);
      fflush(stdout);
    } // End MultiPV loop

    // Restore position after each depth (safety check)
    *pos = saved_pos;

    // Check if time is up, if so, exit depth loop
    if (is_time_up(state))
      break;
  }

  return best_move;
}
