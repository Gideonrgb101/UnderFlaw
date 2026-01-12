#include "movepicker.h"
#include "evaluation.h"
#include "magic.h"
#include "see.h"
#include <stdio.h>
#include <string.h>

static const int PIECE_VALUES[] = {100, 320, 330, 500, 900, 20000, 0};

// Full iterative Static Exchange Evaluation
int see_full(const Position *pos, Move move) { return see(pos, move); }

// Static Exchange Evaluation
int see_capture(const Position *pos, Move move) { return see(pos, move); }

// Sort scored moves by score (descending)
static void sort_moves(ScoredMove *moves, int count) {
  for (int i = 1; i < count; i++) {
    ScoredMove key = moves[i];
    int j = i - 1;
    while (j >= 0 && moves[j].score < key.score) {
      moves[j + 1] = moves[j];
      j--;
    }
    moves[j + 1] = key;
  }
}

// Score a capture move (MVV-LVA)
static int score_capture(const Position *pos, Move move) {
  int to = MOVE_TO(move);
  int from = MOVE_FROM(move);

  int victim = position_piece_at(pos, to);
  if (victim == -1 && to == pos->enpassant)
    victim = PAWN;

  int attacker = position_piece_at(pos, from);

  if (victim == -1 || attacker == -1)
    return 0;

  return PIECE_VALUES[victim] * 10 - PIECE_VALUES[attacker];
}

// Score a quiet move
static int score_quiet(MovePicker *mp, Move move) {
  int from = MOVE_FROM(move);
  int to = MOVE_TO(move);

  if (move == mp->killer1)
    return 1000000;
  if (move == mp->killer2)
    return 900000;
  if (move == mp->countermove)
    return 800000;

  if (mp->history) {
    int piece = position_piece_at(mp->pos, from);
    if (piece != -1) {
      return mp->history[piece * 64 + to];
    }
  }
  return 0;
}

void movepicker_init(MovePicker *mp, const Position *pos, Move tt_move,
                     Move killer1, Move killer2, Move countermove,
                     int history[6][64]) {
  mp->pos = pos;
  mp->tt_move = tt_move;
  mp->killer1 = killer1;
  mp->killer2 = killer2;
  mp->countermove = countermove;
  mp->history = (int *)history;
  mp->stage = STAGE_TT_MOVE;
  mp->num_captures = 0;
  mp->capture_index = 0;
  mp->num_bad_captures = 0;
  mp->bad_capture_index = 0;
  mp->num_quiets = 0;
  mp->quiet_index = 0;
  mp->killer_index = 0;
  mp->quiescence_mode = 0;
}

void movepicker_init_quiescence(MovePicker *mp, const Position *pos,
                                Move tt_move) {
  mp->pos = pos;
  mp->tt_move = tt_move;
  mp->killer1 = MOVE_NONE;
  mp->killer2 = MOVE_NONE;
  mp->countermove = MOVE_NONE;
  mp->history = NULL;
  mp->stage = STAGE_TT_MOVE;
  mp->num_captures = 0;
  mp->capture_index = 0;
  mp->num_bad_captures = 0;
  mp->bad_capture_index = 0;
  mp->num_quiets = 0;
  mp->quiet_index = 0;
  mp->killer_index = 0;
  mp->quiescence_mode = 1;
}

Move movepicker_next(MovePicker *mp) {
  const Position *pos = mp->pos;

  switch (mp->stage) {
  case STAGE_TT_MOVE:
    mp->stage = STAGE_GENERATE_CAPTURES;
    if (mp->tt_move != MOVE_NONE && movegen_is_pseudo_legal(pos, mp->tt_move) &&
        movegen_is_legal(pos, mp->tt_move)) {
      return mp->tt_move;
    }
    // Fall through

  case STAGE_GENERATE_CAPTURES: {
    MoveList captures;
    movegen_captures(pos, &captures);

    for (int i = 0; i < captures.count; i++) {
      Move m = captures.moves[i];
      if (m == mp->tt_move)
        continue;
      if (!movegen_is_legal(pos, m))
        continue;

      int s = see(pos, m);
      if (s >= 0) {
        mp->captures[mp->num_captures].move = m;
        mp->captures[mp->num_captures].score = score_capture(pos, m) + s;
        mp->num_captures++;
      } else {
        mp->bad_captures[mp->num_bad_captures].move = m;
        mp->bad_captures[mp->num_bad_captures].score = s;
        mp->num_bad_captures++;
      }
    }
    sort_moves(mp->captures, mp->num_captures);
    mp->stage = STAGE_GOOD_CAPTURES;
  }
    // Fall through

  case STAGE_GOOD_CAPTURES:
    if (mp->capture_index < mp->num_captures) {
      return mp->captures[mp->capture_index++].move;
    }
    if (mp->quiescence_mode) {
      mp->stage = STAGE_DONE;
      return MOVE_NONE;
    }
    mp->stage = STAGE_KILLERS;
    // Fall through

  case STAGE_KILLERS:
    while (mp->killer_index < 2) {
      Move killer = (mp->killer_index == 0) ? mp->killer1 : mp->killer2;
      mp->killer_index++;
      if (killer != MOVE_NONE && killer != mp->tt_move &&
          !MOVE_IS_CAPTURE(killer) && movegen_is_pseudo_legal(pos, killer) &&
          movegen_is_legal(pos, killer)) {
        return killer;
      }
    }
    if (mp->countermove != MOVE_NONE && mp->countermove != mp->tt_move &&
        mp->countermove != mp->killer1 && mp->countermove != mp->killer2 &&
        !MOVE_IS_CAPTURE(mp->countermove) &&
        movegen_is_pseudo_legal(pos, mp->countermove) &&
        movegen_is_legal(pos, mp->countermove)) {
      mp->stage = STAGE_GENERATE_QUIETS;
      return mp->countermove;
    }
    mp->stage = STAGE_GENERATE_QUIETS;
    // Fall through

  case STAGE_GENERATE_QUIETS: {
    MoveList all_moves;
    movegen_all(pos, &all_moves);

    for (int i = 0; i < all_moves.count; i++) {
      Move m = all_moves.moves[i];
      if (m == mp->tt_move || m == mp->killer1 || m == mp->killer2 ||
          m == mp->countermove || MOVE_IS_CAPTURE(m) ||
          !movegen_is_legal(pos, m))
        continue;

      mp->quiets[mp->num_quiets].move = m;
      mp->quiets[mp->num_quiets].score = score_quiet(mp, m);
      mp->num_quiets++;
    }
    sort_moves(mp->quiets, mp->num_quiets);
    mp->stage = STAGE_QUIETS;
  }
    // Fall through

  case STAGE_QUIETS:
    if (mp->quiet_index < mp->num_quiets) {
      return mp->quiets[mp->quiet_index++].move;
    }
    mp->stage = STAGE_BAD_CAPTURES;
    // Fall through

  case STAGE_BAD_CAPTURES:
    if (mp->bad_capture_index == 0 && mp->num_bad_captures > 0) {
      sort_moves(mp->bad_captures, mp->num_bad_captures);
    }
    if (mp->bad_capture_index < mp->num_bad_captures) {
      return mp->bad_captures[mp->bad_capture_index++].move;
    }
    mp->stage = STAGE_DONE;
    // Fall through

  case STAGE_DONE:
  default:
    return MOVE_NONE;
  }
}
