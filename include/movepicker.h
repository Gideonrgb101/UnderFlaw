#ifndef MOVEPICKER_H
#define MOVEPICKER_H

#include "movegen.h"
#include "position.h"
#include "types.h"


// Move picker stages
typedef enum {
  STAGE_TT_MOVE,           // Stage 1: Hash move
  STAGE_GENERATE_CAPTURES, // Generate captures
  STAGE_GOOD_CAPTURES,     // Stage 2: Winning/equal captures (SEE >= 0)
  STAGE_KILLERS,           // Stage 3: Killer moves
  STAGE_GENERATE_QUIETS,   // Generate quiet moves
  STAGE_QUIETS,            // Stage 4: Non-captures sorted by history
  STAGE_BAD_CAPTURES,      // Stage 5: Losing captures (SEE < 0)
  STAGE_DONE               // No more moves
} MovePickerStage;

// Move with score for sorting
typedef struct {
  Move move;
  int score;
} ScoredMove;

// Move picker state
typedef struct {
  const Position *pos;
  Move tt_move;
  Move killer1;
  Move killer2;
  Move countermove; // Move that refuted previous move
  int *history;     // Pointer to history table

  MovePickerStage stage;

  // Move lists
  ScoredMove captures[64];
  int num_captures;
  int capture_index;

  ScoredMove bad_captures[64];
  int num_bad_captures;
  int bad_capture_index;

  ScoredMove quiets[256];
  int num_quiets;
  int quiet_index;

  int killer_index;

  // For quiescence search (captures only)
  int quiescence_mode;
} MovePicker;

// Initialize move picker for main search
void movepicker_init(MovePicker *mp, const Position *pos, Move tt_move,
                     Move killer1, Move killer2, Move countermove,
                     int history[6][64]);

// Initialize move picker for quiescence search (captures only)
void movepicker_init_quiescence(MovePicker *mp, const Position *pos,
                                Move tt_move);

// Get next move (returns MOVE_NONE when done)
Move movepicker_next(MovePicker *mp);

// SEE (Static Exchange Evaluation) - returns estimated value of capture
// exchange
int see_capture(const Position *pos, Move move);

// Full iterative SEE - more accurate but slower
int see_full(const Position *pos, Move move);

#endif // MOVEPICKER_H
