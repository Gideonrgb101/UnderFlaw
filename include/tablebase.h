#ifndef TABLEBASE_H
#define TABLEBASE_H

#include "position.h"
#include "types.h"
#include <stdbool.h>

// ===== SYZYGY TABLEBASE SUPPORT =====
// This module provides an interface to Syzygy endgame tablebases (.rtbw/.rtbz
// files) allowing the engine to play endgames perfectly when piece count is low
// enough.

// Maximum number of pieces supported (including kings)
// Standard Syzygy tables support up to 6-piece (and some 7-piece) positions
#define TB_MAX_PIECES 6

// Tablebase probe results
typedef enum {
  TB_RESULT_FAILED = -1,   // Probe failed (TB not available, position invalid)
  TB_RESULT_UNKNOWN = 0,   // Position not in tablebase
  TB_WDL_LOSS = 1,         // Side to move loses
  TB_WDL_BLESSED_LOSS = 2, // Loss but draw under 50-move rule
  TB_WDL_DRAW = 3,         // Draw
  TB_WDL_CURSED_WIN = 4,   // Win but draw under 50-move rule
  TB_WDL_WIN = 5           // Side to move wins
} TBResult;

// DTZ (Distance to Zeroing) result
// Positive = moves to win, Negative = moves to loss, 0 = draw
typedef struct {
  TBResult wdl;   // Win/Draw/Loss result
  int dtz;        // Distance to zeroing (capture/pawn move)
  Move best_move; // Best move from tablebase (if available)
  bool from_dtz;  // True if DTZ table was probed
} TBProbeResult;

// Tablebase configuration
typedef struct {
  char path[1024]; // Path to Syzygy tablebase files
  int max_pieces;  // Maximum pieces to probe (default: TB_MAX_PIECES)
  bool use_wdl;    // Use WDL tables
  bool use_dtz;    // Use DTZ tables for root moves
  bool enabled;    // Tablebases enabled

  // Statistics
  uint64_t wdl_probes; // Number of WDL probes
  uint64_t wdl_hits;   // Successful WDL probes
  uint64_t dtz_probes; // Number of DTZ probes
  uint64_t dtz_hits;   // Successful DTZ probes
} TBConfig;

// Global tablebase configuration
extern TBConfig tb_config;

// ===== INITIALIZATION =====

// Initialize tablebase system
// Returns true if at least some tablebases were found
bool tb_init(const char *path);

// Free tablebase resources
void tb_free(void);

// Check if tablebases are available
bool tb_available(void);

// Get the maximum number of pieces supported by loaded tablebases
int tb_max_cardinality(void);

// ===== PROBING FUNCTIONS =====

// Probe WDL (Win/Draw/Loss) tables
// This is fast and can be used during search
// Returns: TB_WIN, TB_LOSS, TB_DRAW, TB_CURSED_WIN, TB_BLESSED_LOSS, or
// TB_RESULT_UNKNOWN
TBResult tb_probe_wdl(const Position *pos);

// Probe DTZ (Distance to Zeroing) tables
// This is slower and typically used at root for best move selection
// Returns full probe result including DTZ value and best move
TBProbeResult tb_probe_dtz(const Position *pos);

// Probe root position for best move according to tablebases
// Returns the best move, or MOVE_NONE if probe fails
// Also fills in wdl and dtz values if pointers are non-NULL
Move tb_probe_root(const Position *pos, TBResult *wdl, int *dtz);

// ===== UTILITY FUNCTIONS =====

// Get total piece count (excluding kings)
int tb_piece_count(const Position *pos);

// Check if position is eligible for tablebase probe
// (correct piece count, no castling rights, etc.)
bool tb_probe_eligible(const Position *pos);

// Convert WDL result to a search score
// Adjusts based on DTZ to prefer shorter wins/longer losses
Score tb_wdl_to_score(TBResult wdl, int dtz, int ply);

// Convert tablebase result to string for debug output
const char *tb_result_to_string(TBResult result);

// ===== SEARCH INTEGRATION =====

// Called from search to probe tablebases at appropriate nodes
// Returns true if TB provided a score, which is stored in *score
bool tb_probe_in_search(const Position *pos, int depth, int ply, Score *score);

// Called at root to filter/order moves based on tablebase results
// Returns number of winning moves found, or -1 if probe failed
int tb_filter_root_moves(const Position *pos, Move *moves, int num_moves,
                         int *wdl_scores);

// ===== STATISTICS =====

// Reset tablebase statistics
void tb_reset_stats(void);

// Get statistics string
void tb_get_stats(char *buffer, int buffer_size);

#endif // TABLEBASE_H
