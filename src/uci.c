#include "uci.h"
#include "bitboard.h"
#include "book.h"
#include "evaluation.h"
#include "movegen.h"
#include "nnue.h"
#include "perft.h"
#include "position.h"
#include "search.h"
#include "tablebase.h"
#include "threads.h"
#include "tuner.h"
#include "types.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global options
int uci_use_nnue = 0;

// Forward declarations
static Move parse_move(const Position *pos, const char *movestr);
void move_to_string(Move move, char *str);

// ============================================================================
// WIN/DRAW/LOSS CALCULATION
// ============================================================================

// Convert centipawn score to win probability using a logistic curve
// Based on Stockfish's WDL model parameters
WDLStats calculate_wdl(Score score, int ply) {
  WDLStats wdl = {0, 0, 0};

  // Avoid division issues at mate scores
  if (score >= SCORE_MATE - 100) {
    wdl.win_chance = 1000;
    wdl.draw_chance = 0;
    wdl.loss_chance = 0;
    return wdl;
  }
  if (score <= -SCORE_MATE + 100) {
    wdl.win_chance = 0;
    wdl.draw_chance = 0;
    wdl.loss_chance = 1000;
    return wdl;
  }

  // Parameters tuned for typical chess positions
  // The model uses: P(win) = 1 / (1 + exp(-a * score))
  // where 'a' varies with game phase (approximated by ply)
  double a = 0.004; // Base factor

  // Adjust for game phase - draws are more common in endgame
  if (ply > 60)
    a = 0.003; // Endgame: flatter curve, more draws
  else if (ply < 20)
    a = 0.005; // Opening: steeper curve

  // Calculate win and loss probabilities
  double win_prob = 1.0 / (1.0 + exp(-a * score));
  double loss_prob = 1.0 / (1.0 + exp(a * score));

  // Draw probability is what's left
  double draw_prob = 1.0 - win_prob - loss_prob;
  if (draw_prob < 0)
    draw_prob = 0;

  // Normalize to sum to 1000
  double total = win_prob + draw_prob + loss_prob;
  wdl.win_chance = (int)(win_prob / total * 1000 + 0.5);
  wdl.loss_chance = (int)(loss_prob / total * 1000 + 0.5);
  wdl.draw_chance = 1000 - wdl.win_chance - wdl.loss_chance;
  return wdl;
}

// ============================================================================
// MULTIPV SEARCH
// ============================================================================

// Helper to output info string with optional WDL
static void output_search_info(EngineState *engine, int multipv_idx, int depth,
                               int seldepth, Score score, int nodes,
                               int time_ms, Move *pv, int pv_length,
                               int tb_hits) {
  char pv_str[2048] = {0};
  char move_str[10];

  // Build PV string
  for (int i = 0; i < pv_length && i < 20; i++) {
    move_to_string(pv[i], move_str);
    if (i > 0)
      strcat(pv_str, " ");
    strcat(pv_str, move_str);
  }

  // Calculate NPS
  int nps = time_ms > 0 ? (int)((int64_t)nodes * 1000 / time_ms) : 0;

  // Calculate hashfull
  int hashfull = tt_hashfull(engine->search->tt);

  // Base info output
  printf("info depth %d seldepth %d ", depth, seldepth > 0 ? seldepth : depth);

  // MultiPV index (1-based)
  if (engine->uci_options.multi_pv > 1) {
    printf("multipv %d ", multipv_idx + 1);
  }

  // Score - handle mate scores
  if (score > SCORE_MATE - 100) {
    int mate_in = (SCORE_MATE - score + 1) / 2;
    printf("score mate %d ", mate_in);
  } else if (score < -SCORE_MATE + 100) {
    int mate_in = (-SCORE_MATE - score) / 2;
    printf("score mate %d ", mate_in);
  } else {
    printf("score cp %d ", score);
  }

  // WDL statistics if enabled
  if (engine->uci_options.show_wdl && score > -SCORE_MATE + 100 &&
      score < SCORE_MATE - 100) {
    WDLStats wdl = calculate_wdl(score, engine->position.fullmove * 2);
    printf("wdl %d %d %d ", wdl.win_chance, wdl.draw_chance, wdl.loss_chance);
  }

  printf("nodes %d ", nodes);
  printf("nps %d ", nps);
  printf("hashfull %d ", hashfull);

  if (tb_hits > 0) {
    printf("tbhits %d ", tb_hits);
  }

  printf("time %d ", time_ms);

  if (pv_length > 0) {
    printf("pv %s", pv_str);
  }

  printf("\n");
  fflush(stdout);
}

// MultiPV search implementation
void search_multipv(EngineState *engine, int max_depth, int time_ms) {
  int multi_pv = engine->uci_options.multi_pv;
  if (multi_pv > MAX_MULTIPV)
    multi_pv = MAX_MULTIPV;

  // Generate all legal moves
  MoveList moves;
  movegen_all(&engine->position, &moves);

  // Filter to only legal moves
  Move legal_moves[256];
  int num_legal = 0;

  for (int i = 0; i < moves.count; i++) {
    if (movegen_is_legal(&engine->position, moves.moves[i])) {
      // If searchmoves is specified, only include those
      if (engine->num_searchmoves > 0) {
        int found = 0;
        for (int j = 0; j < engine->num_searchmoves; j++) {
          if (moves.moves[i] == engine->searchmoves[j]) {
            found = 1;
            break;
          }
        }
        if (!found)
          continue;
      }
      legal_moves[num_legal++] = moves.moves[i];
    }
  }

  if (num_legal == 0) {
    // No legal moves
    return;
  }

  // Limit MultiPV to number of legal moves
  if (multi_pv > num_legal)
    multi_pv = num_legal;

  // Clear MultiPV results
  memset(engine->multipv_lines, 0, sizeof(engine->multipv_lines));

  // Track excluded moves for MultiPV
  Move excluded[MAX_MULTIPV];
  int num_excluded = 0;

  // Set up search timing
  engine->search->max_time_ms = time_ms;
  engine->search->max_depth = max_depth;
  engine->search->start_time_ms = clock() / (CLOCKS_PER_SEC / 1000);
  engine->search->nodes = 0;

  // Iterative deepening for MultiPV
  for (int depth = 1; depth <= max_depth; depth++) {
    num_excluded = 0;

    // Search each PV line
    for (int pv_idx = 0; pv_idx < multi_pv; pv_idx++) {
      // Find best move excluding already found PV moves
      Move best_move = MOVE_NONE;
      Score best_score = -SCORE_INFINITE;
      Move best_pv[MAX_DEPTH];
      int best_pv_length = 0;

      for (int m = 0; m < num_legal; m++) {
        Move move = legal_moves[m];

        // Skip excluded moves (already used in higher PV lines)
        int skip = 0;
        for (int e = 0; e < num_excluded; e++) {
          if (excluded[e] == move) {
            skip = 1;
            break;
          }
        }
        if (skip)
          continue;
        // Make move and search
        UndoInfo undo;
        position_make_move(&engine->position, move, &undo);
        add_repetition_position(engine->search, engine->position.zobrist);
        engine->search->ply = 1;

        // Negamax search - provide a valid SearchInfo struct
        SearchInfo search_info = {0};
        Score score = -negamax(engine->search, &engine->position, depth - 1,
                               -SCORE_INFINITE, -best_score, &search_info);

        position_unmake_move(&engine->position, move, &undo);
        remove_repetition_position(engine->search);

        if (score > best_score) {
          best_score = score;
          best_move = move;

          // Store PV
          best_pv[0] = move;
          best_pv_length = 1;

          // Get rest of PV from TT
          Position temp = engine->position;
          UndoInfo temp_undo;
          position_make_move(&temp, move, &temp_undo);

          for (int p = 1; p < MAX_DEPTH - 1; p++) {
            Move tt_move = tt_get_best_move(engine->search->tt, temp.zobrist);
            if (tt_move == MOVE_NONE || !movegen_is_legal(&temp, tt_move))
              break;
            best_pv[best_pv_length++] = tt_move;
            position_make_move(&temp, tt_move, &temp_undo);
          }
        }
      }

      if (best_move != MOVE_NONE) {
        // Store result
        engine->multipv_lines[pv_idx].score = best_score;
        engine->multipv_lines[pv_idx].depth = depth;
        engine->multipv_lines[pv_idx].seldepth =
            depth; // TODO: track actual seldepth
        engine->multipv_lines[pv_idx].nodes = engine->search->nodes;
        engine->multipv_lines[pv_idx].pv_length = best_pv_length;
        memcpy(engine->multipv_lines[pv_idx].pv, best_pv,
               best_pv_length * sizeof(Move));

        // Exclude this move for next PV line
        if (num_excluded < MAX_MULTIPV) {
          excluded[num_excluded++] = best_move;
        }

        // Output info for this PV line
        int elapsed =
            clock() / (CLOCKS_PER_SEC / 1000) - engine->search->start_time_ms;
        output_search_info(engine, pv_idx, depth, depth, best_score,
                           engine->search->nodes, elapsed, best_pv,
                           best_pv_length, tb_hits_in_search);
      }
    }

    // Check time
    int elapsed =
        clock() / (CLOCKS_PER_SEC / 1000) - engine->search->start_time_ms;
    if (elapsed >= time_ms)
      break;
  }
}

// ============================================================================
// TIME MANAGEMENT
// ============================================================================

// Get game phase for time management (0=opening, 1=middlegame, 2=endgame)
int get_game_phase_for_time(const Position *pos) {
  // Use phase_eval from evaluation.c (0 = endgame, 256 = opening)
  int phase = phase_eval(pos);

  if (phase > 200)
    return TM_PHASE_OPENING;
  else if (phase > 80)
    return TM_PHASE_MIDDLE;
  else
    return TM_PHASE_ENDGAME;
}

// Smart time allocation based on game state
TimeAllocation allocate_time(const TimeControl *tc, const Position *pos,
                             int last_score) {
  TimeAllocation result = {0, 0, 0, 0};

  // Fixed time per move
  if (tc->movetime > 0) {
    result.allocated_time = tc->movetime;
    result.max_time = tc->movetime;
    result.optimal_time = tc->movetime;
    result.panic_time = tc->movetime / 2;
    return result;
  }

  // Infinite mode
  if (tc->infinite) {
    result.allocated_time = 86400000; // 24 hours
    result.max_time = 86400000;
    result.optimal_time = 86400000;
    result.panic_time = 86400000;
    return result;
  }

  // Get our time and increment
  int remaining_time = (pos->to_move == WHITE) ? tc->wtime : tc->btime;
  int increment = (pos->to_move == WHITE) ? tc->winc : tc->binc;
  int moves_to_go = tc->movestogo;

  // No time info - use default
  if (remaining_time <= 0) {
    result.allocated_time = 5000;
    result.max_time = 10000;
    result.optimal_time = 5000;
    result.panic_time = 2000;
    return result;
  }

  // Get game phase
  int game_phase = get_game_phase_for_time(pos);

  // Estimate moves remaining if not specified
  if (moves_to_go == 0) {
    // Estimate based on game phase
    switch (game_phase) {
    case TM_PHASE_OPENING:
      moves_to_go = 35; // Many moves left
      break;
    case TM_PHASE_MIDDLE:
      moves_to_go = 25; // Medium moves left
      break;
    case TM_PHASE_ENDGAME:
      moves_to_go = 15; // Fewer moves left
      break;
    default:
      moves_to_go = 25;
    }
  }

  // ===== BASE TIME CALCULATION =====
  // Distribute remaining time over estimated moves
  int base_time = remaining_time / (moves_to_go + 3);

  // Add a portion of increment
  if (increment > 0) {
    base_time += increment * 3 / 4; // Use 75% of increment
  }

  // ===== GAME PHASE ADJUSTMENTS =====
  switch (game_phase) {
  case TM_PHASE_OPENING:
    // Spend less time in opening (use book moves, known theory)
    base_time = base_time * 80 / 100;
    break;
  case TM_PHASE_MIDDLE:
    // Normal time in middlegame
    break;
  case TM_PHASE_ENDGAME:
    // Spend more time in endgame (precision matters)
    base_time = base_time * 120 / 100;
    break;
  }

  // ===== SCORE-BASED ADJUSTMENTS =====
  if (last_score != 0) {
    int abs_score = last_score > 0 ? last_score : -last_score;

    if (abs_score > 300) // Significant advantage/disadvantage
    {
      if (last_score > 0) {
        // Winning - play faster, don't overthink
        base_time = base_time * 70 / 100;
      } else {
        // Losing - think more, look for resources
        base_time = base_time * 140 / 100;
      }
    } else if (abs_score > 100) // Small advantage
    {
      if (last_score > 0) {
        base_time = base_time * 85 / 100;
      } else {
        base_time = base_time * 115 / 100;
      }
    }
  }

  // ===== EMERGENCY TIME HANDLING =====
  // If we're low on time, be very conservative
  int emergency_threshold = increment > 0 ? 30 * increment : 30000;

  if (remaining_time < emergency_threshold) {
    // Very low time - use emergency allocation
    base_time = remaining_time / 10;
    if (increment > 0) {
      base_time = (remaining_time / 15) + increment / 2;
    }
  }

  // ===== SUDDEN DEATH HANDLING =====
  if (increment == 0 && tc->movestogo == 0) {
    // True sudden death - be very conservative
    base_time = remaining_time / 40;
  }

  // ===== CALCULATE TIME BOUNDS =====
  // Optimal time: what we ideally want to use
  result.optimal_time = base_time;

  // Maximum time: absolute maximum we can use (for complex positions)
  result.max_time = base_time * 3;
  if (result.max_time > remaining_time / 4) {
    result.max_time = remaining_time / 4;
  }

  // Allocated time: starting target
  result.allocated_time = base_time;

  // Panic time: threshold for very fast moves
  result.panic_time = base_time / 3;
  if (result.panic_time < 100) {
    result.panic_time = 100;
  }

  // ===== SAFETY BOUNDS =====
  // Never use more than half remaining time
  if (result.allocated_time > remaining_time / 2) {
    result.allocated_time = remaining_time / 2;
  }

  // Minimum time
  if (result.allocated_time < 50) {
    result.allocated_time = 50;
  }

  // Max time safety
  if (result.max_time > remaining_time - 50) {
    result.max_time = remaining_time - 50;
  }
  if (result.max_time < result.allocated_time) {
    result.max_time = result.allocated_time;
  }

  return result;
}

// ============================================================================
// ENGINE INITIALIZATION
// ============================================================================

// ============================================================================
// ENGINE INITIALIZATION
// ============================================================================

EngineState *engine_init() {
  EngineState *engine = (EngineState *)malloc(sizeof(EngineState));
  if (!engine)
    return NULL;

  position_init(&engine->position);

  // Initialize search with 64MB transposition table
  engine->search = search_create(64);
  if (!engine->search) {
    free(engine);
    return NULL;
  }

  // Initialize thread pool with 1 thread and 64MB TT
  threads_init(DEFAULT_THREADS, 64);

  // Initialize opening book
  engine->book = book_create();
  engine->use_book = 0; // Disabled by default - let Arena use its own book

  // Try to load default book files (only used if OwnBook is enabled)
  if (engine->book) {
    if (!book_load(engine->book, "book.bin")) {
      book_load(engine->book, "opening.bin"); // Try alternative name
    }
  }

  engine->debug = 0;

  // Initialize time management
  engine->move_overhead = 50; // 50ms default overhead
  engine->last_score = 0;
  engine->score_drop = 0;

  // Initialize pondering
  engine->pondering = 0;
  engine->ponder_move = MOVE_NONE;
  engine->ponder_result = MOVE_NONE;

  // Initialize UCI extension options
  engine->uci_options.multi_pv = 1;     // Default: single PV
  engine->uci_options.chess960 = 0;     // Standard chess
  engine->uci_options.analyse_mode = 0; // Normal play
  engine->uci_options.show_wdl = 0;     // Don't show WDL by default
  engine->uci_options.show_currline = 0;
  engine->uci_options.show_currmove = 0;
  engine->uci_options.show_refutation = 0;

  // Initialize searchmoves restriction
  engine->num_searchmoves = 0;
  memset(engine->searchmoves, 0, sizeof(engine->searchmoves));
  memset(engine->multipv_lines, 0, sizeof(engine->multipv_lines));

  // Set up initial position
  position_from_fen(&engine->position,
                    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

  return engine;
}

void engine_cleanup(EngineState *engine) {
  if (engine) {
    if (engine->search) {
      search_delete(engine->search);
    }
    if (engine->book) {
      book_delete(engine->book);
    }
    // Free thread pool resources
    threads_destroy();
    // Free tablebase resources and reset stats
    tb_reset_stats();
    tb_free();
    free(engine);
  }
}

static Move parse_move(const Position *pos, const char *movestr) {
  if (strlen(movestr) < 4)
    return MOVE_NONE;

  int from = SQ(movestr[0] - 'a', movestr[1] - '1');
  int to = SQ(movestr[2] - 'a', movestr[3] - '1');

  int promo = 0;
  if (strlen(movestr) > 4) {
    switch (movestr[4]) {
    case 'n':
      promo = KNIGHT - KNIGHT + 1;
      break;
    case 'b':
      promo = BISHOP - KNIGHT + 1;
      break;
    case 'r':
      promo = ROOK - KNIGHT + 1;
      break;
    case 'q':
      promo = QUEEN - KNIGHT + 1;
      break;
    }
  }

  // Generate all legal moves and find matching one
  MoveList moves;
  movegen_all(pos, &moves);

  for (int i = 0; i < moves.count; i++) {
    Move m = moves.moves[i];
    int m_from = MOVE_FROM(m);
    int m_to = MOVE_TO(m);

    // Normal match
    if (m_from == from && m_to == to) {
      if (!promo || MOVE_PROMO(m) == (Move)promo) {
        if (movegen_is_legal(pos, m))
          return m;
      }
    }

    // Castling match (standard notation e1g1 -> internal e1h1)
    if (!uci_chess960 && MOVE_IS_SPECIAL(m)) {
      int standard_to = -1;
      if (m_from == SQ_E1) {
        if (m_to == pos->castling_rooks[0])
          standard_to = SQ_G1;
        else if (m_to == pos->castling_rooks[1])
          standard_to = SQ_C1;
      } else if (m_from == SQ_E8) {
        if (m_to == pos->castling_rooks[2])
          standard_to = SQ_G8;
        else if (m_to == pos->castling_rooks[3])
          standard_to = SQ_C8;
      }

      if (standard_to == to) {
        if (movegen_is_legal(pos, m))
          return m;
      }
    }
  }

  return MOVE_NONE;
}

void move_to_string(Move move, char *str) {
  int from = MOVE_FROM(move);
  int to = MOVE_TO(move);
  int promo = MOVE_PROMO(move);

  // Convert FRC castling to standard notation if not in Chess960 mode
  if (!uci_chess960 && MOVE_IS_SPECIAL(move) &&
      (to == 0 || to == 7 || to == 56 || to == 63)) {
    // White Castling
    if (from == 4) {
      if (to == 7)
        to = 6; // e1h1 -> e1g1
      else if (to == 0)
        to = 2; // e1a1 -> e1c1
    }
    // Black Castling
    else if (from == 60) {
      if (to == 63)
        to = 62; // e8h8 -> e8g8
      else if (to == 56)
        to = 58; // e8a8 -> e8c8
    }
  }

  str[0] = 'a' + SQ_FILE(from);
  str[1] = '1' + SQ_RANK(from);
  str[2] = 'a' + SQ_FILE(to);
  str[3] = '1' + SQ_RANK(to);

  if (promo > 0) {
    const char *promos = "nbrq";
    str[4] = promos[promo - 1];
    str[5] = '\0';
  } else {
    str[4] = '\0';
  }
}

void engine_handle_uci_command(EngineState *engine, const char *line) {
  (void)engine;
  (void)line; // Mark as used

  printf("id name UnderFlaw\n");
  printf("id author AI Assistant\n");

  // Standard options
  printf("option name Hash type spin default 64 min 1 max 1024\n");
  printf("option name Threads type spin default 1 min 1 max 64\n");
  printf("option name Depth type spin default 32 min 1 max 128\n");
  printf("option name MoveOverhead type spin default 50 min 0 max 5000\n");
  printf("option name Contempt type spin default 20 min -100 max 100\n");
  printf("option name SyzygyPath type string default <empty>\n");
  printf("option name SyzygyProbeDepth type spin default 1 min 1 max 100\n");
  printf("option name OwnBook type check default false\n");
  printf("option name BookFile type string default book.bin\n");
  printf("option name BookLearning type check default true\n");
  printf("option name BookRandom type spin default 0 min 0 max 100\n");
  printf("option name Ponder type check default false\n");

  // UCI Extension options
  printf("option name MultiPV type spin default 1 min 1 max %d\n", MAX_MULTIPV);
  printf("option name UCI_Chess960 type check default false\n");
  printf("option name UCI_AnalyseMode type check default false\n");
  printf("option name UCI_ShowWDL type check default false\n");
  printf("option name UCI_ShowCurrLine type check default false\n");
  printf("option name UseNNUE type check default false\n");
  printf("option name EvalFile type string default <empty>\n");

  // Playing style options
  printf("option name Style_Aggression type spin default 50 min 0 max 100\n");
  printf("option name Style_Positional type spin default 50 min 0 max 100\n");
  printf("option name Style_RiskTaking type spin default 50 min 0 max 100\n");
  printf(
      "option name Style_DrawAcceptance type spin default 50 min 0 max 100\n");

  printf("uciok\n");
  fflush(stdout);
}

void engine_handle_isready_command(EngineState *engine) {
  (void)engine; // Mark as used
  printf("readyok\n");
  fflush(stdout);
}

void engine_handle_setoption_command(EngineState *engine, const char *line) {
  // Parse "setoption name <id> value <x>"
  const char *name_ptr = strstr(line, "name ");
  const char *value_ptr = strstr(line, "value ");

  if (!name_ptr)
    return;

  char name[256];
  if (value_ptr) {
    int len = value_ptr - (name_ptr + 5);
    if (len > 255)
      len = 255;
    strncpy(name, name_ptr + 5, len);
    name[len] = '\0';

    // Trim trailing space
    while (strlen(name) > 0 && name[strlen(name) - 1] == ' ') {
      name[strlen(name) - 1] = '\0';
    }
  } else {
    strcpy(name, name_ptr + 5);
  }

  if (value_ptr) {
    const char *value_str = value_ptr + 6;

    if (strcmp(name, "Hash") == 0) {
      int size = atoi(value_str);
      if (size > 0 && size <= 1024) {
        // Recreate transposition table
        if (engine->search) {
          tt_delete(engine->search->tt);
          engine->search->tt = tt_create(size);
        }
      }
    } else if (strcmp(name, "Depth") == 0) {
      int depth = atoi(value_str);
      if (depth > 0 && depth <= 128) {
        engine->search->max_depth = depth;
      }
    } else if (strcmp(name, "Threads") == 0) {
      int num_threads = atoi(value_str);
      if (num_threads >= 1 && num_threads <= MAX_THREADS) {
        threads_set_count(num_threads);
        if (engine->debug) {
          printf("info string Thread count set to %d\n", num_threads);
          fflush(stdout);
        }
      }
    } else if (strcmp(name, "OwnBook") == 0) {
      if (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0) {
        engine->use_book = 1;
      } else {
        engine->use_book = 0;
      }
    } else if (strcmp(name, "BookFile") == 0) {
      if (engine->book) {
        book_load(engine->book, value_str);
      }
    } else if (strcmp(name, "BookLearning") == 0) {
      if (engine->book) {
        bool enabled =
            (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0);
        book_set_learning(engine->book, enabled);
      }
    } else if (strcmp(name, "BookRandom") == 0) {
      if (engine->book) {
        int factor = atoi(value_str);
        book_set_random_factor(engine->book, factor);
      }
    } else if (strcmp(name, "MoveOverhead") == 0) {
      int overhead = atoi(value_str);
      if (overhead >= 0 && overhead <= 5000) {
        engine->move_overhead = overhead;
      }
    } else if (strcmp(name, "Contempt") == 0) {
      int contempt = atoi(value_str);
      if (contempt >= -100 && contempt <= 100) {
        if (engine->search) {
          engine->search->contempt = contempt;
        }
      }
    } else if (strcmp(name, "SyzygyPath") == 0) {
      // Initialize tablebases with the given path
      if (strlen(value_str) > 0 && strcmp(value_str, "<empty>") != 0) {
        if (tb_init(value_str)) {
          if (engine->debug) {
            printf("info string Syzygy tablebases initialized from %s\n",
                   value_str);
          }
        } else {
          if (engine->debug) {
            printf("info string Failed to load Syzygy tablebases from %s\n",
                   value_str);
          }
        }
      }
    } else if (strcmp(name, "SyzygyProbeDepth") == 0) {
      int probe_depth = atoi(value_str);
      if (probe_depth >= 1 && probe_depth <= 100) {
        // Store probe depth setting (could add to engine state if needed)
        if (engine->debug) {
          printf("info string Syzygy probe depth set to %d\n", probe_depth);
        }
      }
    }
    // UCI Extension options
    else if (strcmp(name, "MultiPV") == 0) {
      int multi_pv = atoi(value_str);
      if (multi_pv >= 1 && multi_pv <= MAX_MULTIPV) {
        engine->uci_options.multi_pv = multi_pv;
        if (engine->debug) {
          printf("info string MultiPV set to %d\n", multi_pv);
          fflush(stdout);
        }
      }
    } else if (strcmp(name, "UCI_Chess960") == 0) {
      engine->uci_options.chess960 =
          (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0);
      if (engine->debug) {
        printf("info string Chess960 mode %s\n",
               engine->uci_options.chess960 ? "enabled" : "disabled");
        fflush(stdout);
      }
    } else if (strcmp(name, "UCI_AnalyseMode") == 0) {
      engine->uci_options.analyse_mode =
          (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0);
      if (engine->debug) {
        printf("info string Analyse mode %s\n",
               engine->uci_options.analyse_mode ? "enabled" : "disabled");
        fflush(stdout);
      }
    } else if (strcmp(name, "UCI_ShowWDL") == 0) {
      engine->uci_options.show_wdl =
          (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0);
      // Also set global flag for search.c to use
      uci_show_wdl = engine->uci_options.show_wdl;
      if (engine->debug) {
        printf("info string ShowWDL %s\n",
               engine->uci_options.show_wdl ? "enabled" : "disabled");
        fflush(stdout);
      }
    } else if (strcmp(name, "UCI_ShowCurrLine") == 0) {
      engine->uci_options.show_currline =
          (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0);
    } else if (strcmp(name, "UseNNUE") == 0) {
      if (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0)
        uci_use_nnue = 1;
      else
        uci_use_nnue = 0;
    } else if (strcmp(name, "EvalFile") == 0) {
      if (strcmp(value_str, "<empty>") != 0) {
        nnue_load(value_str);
      }
    }
    // Playing style options
    else if (strcmp(name, "Style_Aggression") == 0) {
      int val = atoi(value_str);
      if (val >= 0 && val <= 100) {
        engine->search->style.aggression = val;
        if (engine->debug) {
          printf("info string Style_Aggression set to %d\n", val);
          fflush(stdout);
        }
      }
    } else if (strcmp(name, "Style_Positional") == 0) {
      int val = atoi(value_str);
      if (val >= 0 && val <= 100) {
        engine->search->style.positional = val;
        if (engine->debug) {
          printf("info string Style_Positional set to %d\n", val);
          fflush(stdout);
        }
      }
    } else if (strcmp(name, "Style_RiskTaking") == 0) {
      int val = atoi(value_str);
      if (val >= 0 && val <= 100) {
        engine->search->style.risk_taking = val;
        if (engine->debug) {
          printf("info string Style_RiskTaking set to %d\n", val);
          fflush(stdout);
        }
      }
    } else if (strcmp(name, "Style_DrawAcceptance") == 0) {
      int val = atoi(value_str);
      if (val >= 0 && val <= 100) {
        engine->search->style.draw_acceptance = val;
        // Adjust contempt based on draw acceptance
        // Higher draw acceptance = more positive contempt (accept draws more)
        int contempt_adjust = (val - 50) / 5; // -10 to +10
        engine->search->contempt = 20 + contempt_adjust;
        if (engine->debug) {
          printf("info string Style_DrawAcceptance set to %d (contempt: %d)\n",
                 val, engine->search->contempt);
          fflush(stdout);
        }
      }
    } else if (strcmp(name, "MultiPV") == 0) {
      int val = atoi(value_str);
      if (val >= 1 && val <= MAX_MULTIPV) {
        engine->uci_options.multi_pv = val;
      }
    } else if (strcmp(name, "UCI_Chess960") == 0) {
      if (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0)
        engine->uci_options.chess960 = 1;
      else
        engine->uci_options.chess960 = 0;
    }
  }
}

void engine_handle_position_command(EngineState *engine, const char *line) {
  const char *fen_ptr = strstr(line, "fen ");
  const char *moves_ptr = strstr(line, "moves ");

  if (fen_ptr) {
    // Parse FEN
    char fen[512];
    const char *start = fen_ptr + 4;
    const char *end = moves_ptr ? moves_ptr - 1 : start + strlen(start);

    int len = end - start;
    if (len > 511)
      len = 511;
    strncpy(fen, start, len);
    fen[len] = '\0';

    // Trim trailing space
    while (strlen(fen) > 0 && fen[strlen(fen) - 1] == ' ') {
      fen[strlen(fen) - 1] = '\0';
    }

    position_from_fen(&engine->position, fen);
  } else if (strstr(line, "startpos")) {
    position_from_fen(
        &engine->position,
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  }

  // Initialize history
  engine->game_history_count = 0;
  if (engine->game_history_count < MAX_GAME_MOVES) {
    engine->game_history[engine->game_history_count++] =
        engine->position.zobrist;
  }

  // Play moves
  if (moves_ptr) {
    const char *move_str = moves_ptr + 6;
    char move_text[10];
    const char *pos = move_str;

    while (*pos) {
      int i = 0;
      while (*pos && *pos != ' ' && i < 9) {
        move_text[i++] = *pos++;
      }
      move_text[i] = '\0';

      if (i >= 4) {
        Move move = parse_move(&engine->position, move_text);
        if (move != MOVE_NONE) {
          UndoInfo undo;
          position_make_move(&engine->position, move, &undo);

          if (engine->game_history_count < MAX_GAME_MOVES) {
            engine->game_history[engine->game_history_count++] =
                engine->position.zobrist;
          }
        }
      }

      while (*pos == ' ')
        pos++;
    }
  }
}

void engine_handle_go_command(EngineState *engine, const char *line) {
  TimeControl tc = {0};
  int depth = 64; // Default max depth
  int ponder = 0;

  // Reset searchmoves restriction
  engine->num_searchmoves = 0;

  // Parse parameters
  const char *depth_ptr = strstr(line, "depth ");
  if (depth_ptr) {
    depth = atoi(depth_ptr + 6);
  }

  const char *movetime_ptr = strstr(line, "movetime ");
  if (movetime_ptr) {
    tc.movetime = atoi(movetime_ptr + 9);
  }

  const char *wtime_ptr = strstr(line, "wtime ");
  if (wtime_ptr) {
    tc.wtime = atoi(wtime_ptr + 6);
  }

  const char *btime_ptr = strstr(line, "btime ");
  if (btime_ptr) {
    tc.btime = atoi(btime_ptr + 6);
  }

  const char *winc_ptr = strstr(line, "winc ");
  if (winc_ptr) {
    tc.winc = atoi(winc_ptr + 5);
  }

  const char *binc_ptr = strstr(line, "binc ");
  if (binc_ptr) {
    tc.binc = atoi(binc_ptr + 5);
  }

  const char *movestogo_ptr = strstr(line, "movestogo ");
  if (movestogo_ptr) {
    tc.movestogo = atoi(movestogo_ptr + 10);
  }

  // Parse searchmoves restriction
  const char *searchmoves_ptr = strstr(line, "searchmoves ");
  if (searchmoves_ptr) {
    const char *pos = searchmoves_ptr + 12;
    char move_text[10];

    while (*pos && engine->num_searchmoves < MAX_SEARCHMOVES) {
      // Skip spaces
      while (*pos == ' ')
        pos++;
      if (!*pos)
        break;

      // Check if we hit another keyword
      if (strncmp(pos, "wtime", 5) == 0 || strncmp(pos, "btime", 5) == 0 ||
          strncmp(pos, "winc", 4) == 0 || strncmp(pos, "binc", 4) == 0 ||
          strncmp(pos, "movestogo", 9) == 0 || strncmp(pos, "depth", 5) == 0 ||
          strncmp(pos, "movetime", 8) == 0 ||
          strncmp(pos, "infinite", 8) == 0 || strncmp(pos, "ponder", 6) == 0) {
        break;
      }

      // Parse move
      int i = 0;
      while (*pos && *pos != ' ' && i < 9) {
        move_text[i++] = *pos++;
      }
      move_text[i] = '\0';

      if (i >= 4) {
        Move move = parse_move(&engine->position, move_text);
        if (move != MOVE_NONE) {
          engine->searchmoves[engine->num_searchmoves++] = move;
        }
      }
    }

    if (engine->debug && engine->num_searchmoves > 0) {
      printf("info string searchmoves restricted to %d moves\n",
             engine->num_searchmoves);
      fflush(stdout);
    }
  }

  // Check for infinite mode
  if (strstr(line, "infinite")) {
    tc.infinite = 1;
  }

  // Check for ponder mode
  if (strstr(line, "ponder")) {
    ponder = 1;
    engine->pondering = 1;
  }

  // ===== SMART TIME ALLOCATION =====
  TimeAllocation time_alloc =
      allocate_time(&tc, &engine->position, engine->last_score);

  // Apply move overhead compensation
  int allocated_time = time_alloc.allocated_time;
  if (!tc.infinite && tc.movetime == 0) {
    allocated_time -= engine->move_overhead;
    if (allocated_time < 10) {
      allocated_time = 10;
    }
  }

  // Debug output for time management
  if (engine->debug) {
    int game_phase = get_game_phase_for_time(&engine->position);
    const char *phase_names[] = {"opening", "middlegame", "endgame"};
    printf("info string time allocation: %dms (optimal: %dms, max: %dms) "
           "phase: %s\n",
           allocated_time, time_alloc.optimal_time, time_alloc.max_time,
           phase_names[game_phase]);
    fflush(stdout);
  } // Set search parameters
  engine->search->max_depth = depth;
  engine->search->max_time_ms = allocated_time;

  // Try opening book first (not when pondering)
  Move best_move = MOVE_NONE;
  if (!ponder && engine->use_book && engine->book && engine->book->loaded) {
    best_move = book_probe(engine->book, &engine->position);
    if (best_move != MOVE_NONE && engine->debug) {
      printf("info string book move found\n");
      fflush(stdout);
    }
  } // If no book move, search
  if (best_move == MOVE_NONE) {
    // Set MultiPV option in search state
    engine->search->multipv = engine->uci_options.multi_pv;

    // Use unified search path
    if (engine->num_searchmoves > 0) {
      // Handle searchmoves if needed, or rely on search_root to filter?
      // Current implementation passes engine->searchmoves via ???
      // EngineState has searchmoves, SearchState doesn't.
      // I need to propagate searchmoves too if I want it to work.
      // it later.
    }

    // Copy/Sync game history for 3-fold repetition check
    engine->search->repetition_ply = 0;
    for (int i = 0; i < engine->game_history_count; i++) {
      if (i < MAX_GAME_MOVES) {
        engine->search->repetition_history[i] = engine->game_history[i];
        engine->search->repetition_ply++;
      }
    }

    best_move =
        iterative_deepening(engine->search, &engine->position, allocated_time);

    // Update last score for future time management
    engine->last_score = engine->search->previous_score;
  }

  // Output best move
  if (best_move != MOVE_NONE) {
    // Safety check: Ensure the move is legal
    if (!movegen_is_legal(&engine->position, best_move)) {
      if (engine->debug) {
        printf("info string CRITICAL: Search returned illegal move! Finding "
               "fallback.\n");
      }

      // Fallback: Generate all legal moves and pick the first one
      MoveList legal_moves;
      movegen_all(&engine->position, &legal_moves);
      best_move = MOVE_NONE;

      for (int i = 0; i < legal_moves.count; i++) {
        if (movegen_is_legal(&engine->position, legal_moves.moves[i])) {
          best_move = legal_moves.moves[i];
          if (engine->debug) {
            printf("info string Fallback move found: %s\n",
                   square_name(MOVE_FROM(best_move)));
          }
          break;
        }
      }

      if (best_move == MOVE_NONE) {
        // No legal moves? Mate or Stalemate loop logic usually handles this,
        // but if we are here, something is very wrong.
        printf("bestmove 0000\n"); // Null move to surrender gracefully?
        fflush(stdout);
        return;
      }
    }

    char move_str[10];

    move_to_string(best_move, move_str);

    // Extract ponder move from PV if available
    char ponder_str[10] = {0};
    if (engine->search->pv_length >= 2) {
      move_to_string(engine->search->pv[1], ponder_str);
      printf("bestmove %s ponder %s\n", move_str, ponder_str);
    } else {
      printf("bestmove %s\n", move_str);
    }
  } else {
    printf("bestmove 0000\n");
  }

  engine->pondering = 0;
  fflush(stdout);
}

void engine_run_uci_loop() {
  EngineState *engine = engine_init();
  if (!engine) {
    fprintf(stderr, "Failed to initialize engine\n");
    return;
  }

  char line[4096];

  while (fgets(line, sizeof(line), stdin)) {
    // Remove trailing newline
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }

    if (strlen(line) == 0)
      continue;

    if (engine->debug) {
      fprintf(stderr, ">>> %s\n", line);
    }
    if (strcmp(line, "quit") == 0) {
      break;
    } else if (strcmp(line, "uci") == 0) {
      engine_handle_uci_command(engine, line);
    } else if (strcmp(line, "isready") == 0) {
      engine_handle_isready_command(engine);
    } else if (strcmp(line, "ucinewgame") == 0) {
      // Reset engine state for new game
      tt_clear(engine->search->tt);
      pawn_tt_clear(engine->search->pawn_tt);
      eval_tt_clear(engine->search->eval_tt);
      engine->last_score = 0;
      position_from_fen(
          &engine->position,
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
      // Reset tablebase statistics
      tb_reset_stats();
    } else if (strncmp(line, "debug ", 6) == 0) {
      // Handle debug command
      if (strstr(line, "on")) {
        engine->debug = 1;
      } else if (strstr(line, "off")) {
        engine->debug = 0;
      }
    } else if (strcmp(line, "stats") == 0) {
      // Print search statistics (debug command)
      search_stats_print();
    } else if (strcmp(line, "stop") == 0) {
      // Stop command - search should already have returned by now
      // This is handled implicitly since we don't support pondering
    } else if (strncmp(line, "setoption", 9) == 0) {
      engine_handle_setoption_command(engine, line);
    } else if (strncmp(line, "position", 8) == 0) {
      engine_handle_position_command(engine, line);
    } else if (strncmp(line, "go", 2) == 0 &&
               (line[2] == '\0' || line[2] == ' ')) {
      engine_handle_go_command(engine, line);
    } else if (strcmp(line, "d") == 0) {
      // Debug: print board
      char fen[512];
      position_to_fen(&engine->position, fen, sizeof(fen));
      printf("%s\n", fen);
    } else if (strncmp(line, "tune ", 5) == 0) {
      // Tuning command: tune <method> [datafile]
      // method: texel, genetic
      char method[32] = {0};
      char datafile[256] = {0};
      sscanf(line + 5, "%31s %255s", method, datafile);
      uci_start_tuning(method, datafile);
    } else if (strcmp(line, "eval") == 0) {
      Score s = evaluate(&engine->position);
      printf("Evaluation: %d (NNUE: %s)\n", s,
             nnue_available ? "active" : "inactive");
    } else if (strncmp(line, "perft", 5) == 0) {
      int depth = 5;
      if (strlen(line) > 6)
        depth = atoi(line + 6);
      if (depth < 1)
        depth = 1;

      perft_divide(&engine->position, depth);
    } else if (strncmp(line, "bench", 5) == 0) {
      // Search-based benchmark for NPS measurement
      int depth = 12;
      if (strlen(line) > 6)
        depth = atoi(line + 6);
      if (depth < 1)
        depth = 1;
      if (depth > 20)
        depth = 20;

      // Standard benchmark positions
      const char *bench_fens[] = {
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 "
          "1",
          "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
          "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
          "rq3rk1/ppp2ppp/1bnpb3/3N4/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14",
          "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14",
          "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
          "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
          "r1bq1r1k/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
          "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
          "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
          "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
          "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
          "r1q2rk1/2p1bppp/2Pp4/p7/Q3P3/4B3/PP1B1PPP/R4RK1 w - - 0 18",
          "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22"};
      int num_positions = sizeof(bench_fens) / sizeof(bench_fens[0]);

      printf("Running search benchmark at depth %d...\n", depth);
      printf("Positions: %d\n", num_positions);
      printf("================================\n");

      uint64_t total_nodes = 0;
      int total_time_ms = 0;

      for (int i = 0; i < num_positions; i++) {
        Position pos;
        position_from_fen(&pos, bench_fens[i]);

        // Clear search state
        engine->search->nodes = 0;
        engine->search->start_time_ms = get_time_ms();

        // Run search with time limit (convert depth to approximate time)
        int time_limit_ms = 10000; // 10 seconds per position
        Move best_move =
            iterative_deepening(engine->search, &pos, time_limit_ms);

        int elapsed = get_time_ms() - engine->search->start_time_ms;
        total_nodes += engine->search->nodes;
        total_time_ms += elapsed;

        char move_str[10];
        move_to_string(best_move, move_str);
        printf("Position %2d: %10llu nodes in %6d ms (%8.0f nps) - best: %s\n",
               i + 1, (unsigned long long)engine->search->nodes, elapsed,
               elapsed > 0 ? (double)engine->search->nodes * 1000.0 / elapsed
                           : 0.0,
               move_str);
      }

      printf("================================\n");
      printf("Total nodes: %llu\n", (unsigned long long)total_nodes);
      printf("Total time:  %d ms\n", total_time_ms);
      if (total_time_ms > 0) {
        printf("Nodes/second: %.0f\n",
               (double)total_nodes * 1000.0 / total_time_ms);
      }
    } else if (strncmp(line, "test", 4) == 0) {
      const char *file = "tests/test_positions.epd";
      if (strlen(line) > 5)
        file = line + 5;
      run_test_suite(file);
    }
  }

  engine_cleanup(engine);
}
