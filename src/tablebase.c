#include "tablebase.h"
#include "bitboard.h"
#include "movegen.h"
#include "position.h"
#include "tbprobe.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// ===== SYZYGY TABLEBASE IMPLEMENTATION =====
// Integrated with Fathom library for full Syzygy support.

// Global tablebase configuration
TBConfig tb_config = {.path = "",
                      .max_pieces = TB_MAX_PIECES,
                      .use_wdl = true,
                      .use_dtz = true,
                      .enabled = false,
                      .wdl_probes = 0,
                      .wdl_hits = 0,
                      .dtz_probes = 0,
                      .dtz_hits = 0};

// Internal state
static bool tb_initialized = false;

// ===== INITIALIZATION =====

bool tb_init(const char *path) {
  if (!path || strlen(path) == 0) {
    tb_config.enabled = false;
    return false;
  }

  // Store the path
  strncpy(tb_config.path, path, sizeof(tb_config.path) - 1);
  tb_config.path[sizeof(tb_config.path) - 1] = '\0';

  // Initialize Fathom
  if (tb_init_impl(path)) {
    tb_config.enabled = (TB_LARGEST > 0);
    tb_initialized = true;
    if (tb_config.enabled) {
      printf("info string Syzygy tablebases found: %d pieces\n", TB_LARGEST);
    }
    return tb_config.enabled;
  }

  tb_config.enabled = false;
  tb_initialized = true;
  return false;
}

void tb_free(void) {
  // Fathom doesn't have a specific free function in this version
  tb_initialized = false;
  tb_config.enabled = false;
}

bool tb_available(void) { return tb_config.enabled && TB_LARGEST > 0; }

int tb_max_cardinality(void) { return (int)TB_LARGEST; }

// ===== UTILITY FUNCTIONS =====

int tb_piece_count(const Position *pos) { return POPCOUNT(pos->all); }

bool tb_probe_eligible(const Position *pos) {
  // Check piece count
  int pieces = tb_piece_count(pos);
  if (pieces > tb_config.max_pieces || pieces > (int)TB_LARGEST) {
    return false;
  }

  // Syzygy tablebases don't handle positions with castling rights
  if (pos->castling != 0) {
    return false;
  }

  // Must have both kings
  if (!pos->pieces[WHITE][KING] || !pos->pieces[BLACK][KING]) {
    return false;
  }

  return true;
}

// ===== PROBING FUNCTIONS =====

TBResult tb_probe_wdl(const Position *pos) {
  tb_config.wdl_probes++;

  if (!tb_config.enabled || !tb_probe_eligible(pos)) {
    return TB_RESULT_UNKNOWN;
  }

  // Fathom's tb_probe_wdl_impl returns:
  // 0=LOSS, 1=BLESSED_LOSS, 2=DRAW, 3=CURSED_WIN, 4=WIN
  // Or TB_RESULT_FAILED (0xFFFFFFFF)
  unsigned res = tb_probe_wdl_impl(
      pos->occupied[WHITE], pos->occupied[BLACK],
      pos->pieces[WHITE][KING] | pos->pieces[BLACK][KING],
      pos->pieces[WHITE][QUEEN] | pos->pieces[BLACK][QUEEN],
      pos->pieces[WHITE][ROOK] | pos->pieces[BLACK][ROOK],
      pos->pieces[WHITE][BISHOP] | pos->pieces[BLACK][BISHOP],
      pos->pieces[WHITE][KNIGHT] | pos->pieces[BLACK][KNIGHT],
      pos->pieces[WHITE][PAWN] | pos->pieces[BLACK][PAWN],
      pos->enpassant == -1 ? 0 : pos->enpassant, pos->to_move == WHITE);

  if (res == TB_RESULT_FAILED)
    return TB_RESULT_FAILED;

  tb_config.wdl_hits++;

  // Map Fathom results to Engine TBResult
  // Fathom: 0=LOSS, 1=BLESSED_LOSS, 2=DRAW, 3=CURSED_WIN, 4=WIN
  // Engine: 1=LOSS, 2=BLESSED_LOSS, 3=DRAW, 4=CURSED_WIN, 5=WIN
  return (TBResult)(res + 1);
}

TBProbeResult tb_probe_dtz(const Position *pos) {
  TBProbeResult result = {.wdl = TB_RESULT_UNKNOWN,
                          .dtz = 0,
                          .best_move = MOVE_NONE,
                          .from_dtz = false};

  tb_config.dtz_probes++;

  if (!tb_config.enabled || !tb_probe_eligible(pos)) {
    return result;
  }

  unsigned results[TB_MAX_MOVES];
  unsigned res = tb_probe_root_impl(
      pos->occupied[WHITE], pos->occupied[BLACK],
      pos->pieces[WHITE][KING] | pos->pieces[BLACK][KING],
      pos->pieces[WHITE][QUEEN] | pos->pieces[BLACK][QUEEN],
      pos->pieces[WHITE][ROOK] | pos->pieces[BLACK][ROOK],
      pos->pieces[WHITE][BISHOP] | pos->pieces[BLACK][BISHOP],
      pos->pieces[WHITE][KNIGHT] | pos->pieces[BLACK][KNIGHT],
      pos->pieces[WHITE][PAWN] | pos->pieces[BLACK][PAWN], pos->halfmove,
      pos->enpassant == -1 ? 0 : pos->enpassant, pos->to_move == WHITE,
      results);

  if (res == TB_RESULT_FAILED)
    return result;

  tb_config.dtz_hits++;
  result.from_dtz = true;
  result.wdl = (TBResult)(TB_GET_WDL(res) + 1);
  result.dtz = TB_GET_DTZ(res);

  // Convert Fathom move to Engine Move
  unsigned from = TB_GET_FROM(res);
  unsigned to = TB_GET_TO(res);
  unsigned promotes = TB_GET_PROMOTES(res);

  int engine_promo = 0;
  if (promotes == TB_PROMOTES_QUEEN)
    engine_promo = QUEEN;
  else if (promotes == TB_PROMOTES_ROOK)
    engine_promo = ROOK;
  else if (promotes == TB_PROMOTES_BISHOP)
    engine_promo = BISHOP;
  else if (promotes == TB_PROMOTES_KNIGHT)
    engine_promo = KNIGHT;

  int move_flag = FLAG_QUIET;
  if (pos->all & (1ULL << to))
    move_flag = FLAG_CAPTURE;

  result.best_move = MAKE_MOVE(
      from, to, engine_promo ? engine_promo - KNIGHT + 1 : 0, move_flag);

  return result;
}

Move tb_probe_root(const Position *pos, TBResult *wdl, int *dtz) {
  TBProbeResult result = tb_probe_dtz(pos);

  if (wdl)
    *wdl = result.wdl;
  if (dtz)
    *dtz = result.dtz;

  return result.best_move;
}

// ===== SCORE CONVERSION =====

Score tb_wdl_to_score(TBResult wdl, int dtz, int ply) {
  switch (wdl) {
  case TB_WDL_WIN:
    return SCORE_TB_WIN - ply - abs(dtz);
  case TB_WDL_CURSED_WIN:
    return 200 - ply;
  case TB_WDL_DRAW:
    return 0;
  case TB_WDL_BLESSED_LOSS:
    return -200 + ply;
  case TB_WDL_LOSS:
    return -SCORE_TB_WIN + ply + abs(dtz);
  default:
    return 0;
  }
}

const char *tb_result_to_string(TBResult result) {
  switch (result) {
  case TB_WDL_WIN:
    return "win";
  case TB_WDL_CURSED_WIN:
    return "cursed_win";
  case TB_WDL_DRAW:
    return "draw";
  case TB_WDL_BLESSED_LOSS:
    return "blessed_loss";
  case TB_WDL_LOSS:
    return "loss";
  case TB_RESULT_FAILED:
    return "failed";
  case TB_RESULT_UNKNOWN:
    return "unknown";
  default:
    return "???";
  }
}

// ===== SEARCH INTEGRATION =====

bool tb_probe_in_search(const Position *pos, int depth, int ply, Score *score) {
  if (ply == 0)
    return false;
  if (depth > 6 && (ply & 1))
    return false;
  if (!tb_probe_eligible(pos))
    return false;

  TBResult wdl = tb_probe_wdl(pos);

  if (wdl == TB_RESULT_UNKNOWN || wdl == TB_RESULT_FAILED) {
    return false;
  }

  if (wdl == TB_WDL_WIN || wdl == TB_WDL_LOSS) {
    int dtz = (wdl == TB_WDL_WIN) ? 20 : -20;
    *score = tb_wdl_to_score(wdl, dtz, ply);
    return true;
  }

  if (wdl == TB_WDL_CURSED_WIN || wdl == TB_WDL_BLESSED_LOSS) {
    *score = tb_wdl_to_score(wdl, 50, ply);
    return true;
  }

  if (wdl == TB_WDL_DRAW && depth <= 4) {
    *score = 0;
    return true;
  }

  return false;
}

int tb_filter_root_moves(const Position *pos, Move *moves, int num_moves,
                         int *wdl_scores) {
  if (!tb_config.enabled || !tb_probe_eligible(pos)) {
    return -1;
  }

  TBResult pos_wdl = tb_probe_wdl(pos);
  if (pos_wdl == TB_RESULT_UNKNOWN || pos_wdl == TB_RESULT_FAILED) {
    return -1;
  }

  int winning_count = 0;

  for (int i = 0; i < num_moves; i++) {
    Position temp = *pos;
    UndoInfo undo;
    position_make_move(&temp, moves[i], &undo);

    TBResult child_wdl = tb_probe_wdl(&temp);

    int score = 0;
    switch (child_wdl) {
    case TB_WDL_LOSS:
      score = 5;
      winning_count++;
      break;
    case TB_WDL_BLESSED_LOSS:
      score = 4;
      break;
    case TB_WDL_DRAW:
      score = 3;
      break;
    case TB_WDL_CURSED_WIN:
      score = 2;
      break;
    case TB_WDL_WIN:
      score = 1;
      break;
    default:
      score = 0;
      break;
    }

    if (wdl_scores) {
      wdl_scores[i] = score;
    }
  }

  return winning_count;
}

// ===== STATISTICS =====

void tb_reset_stats(void) {
  tb_config.wdl_probes = 0;
  tb_config.wdl_hits = 0;
  tb_config.dtz_probes = 0;
  tb_config.dtz_hits = 0;
}

void tb_get_stats(char *buffer, int buffer_size) {
  double wdl_rate = tb_config.wdl_probes > 0
                        ? 100.0 * tb_config.wdl_hits / tb_config.wdl_probes
                        : 0.0;
  double dtz_rate = tb_config.dtz_probes > 0
                        ? 100.0 * tb_config.dtz_hits / tb_config.dtz_probes
                        : 0.0;

  snprintf(buffer, buffer_size,
           "TB Stats: WDL probes=%llu hits=%llu (%.1f%%), DTZ probes=%llu "
           "hits=%llu (%.1f%%)",
           (unsigned long long)tb_config.wdl_probes,
           (unsigned long long)tb_config.wdl_hits, wdl_rate,
           (unsigned long long)tb_config.dtz_probes,
           (unsigned long long)tb_config.dtz_hits, dtz_rate);
}
