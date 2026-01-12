#ifndef UCI_H
#define UCI_H

#include "book.h"
#include "position.h"
#include "search.h"
#include "types.h"

// Game phases for time management
#define TM_PHASE_OPENING 0
#define TM_PHASE_MIDDLE 1
#define TM_PHASE_ENDGAME 2

// Maximum path length for tablebase path
#define TB_PATH_MAX 1024

// Maximum MultiPV lines
#define MAX_MULTIPV 10

// Maximum searchmoves restriction
#define MAX_SEARCHMOVES 64

// Time control structure
typedef struct {
  int wtime;        // White time remaining (ms)
  int btime;        // Black time remaining (ms)
  int winc;         // White increment (ms)
  int binc;         // Black increment (ms)
  int movestogo;    // Moves until next time control (0 = sudden death)
  int movetime;     // Fixed time per move (0 = not set)
  int depth;        // Max depth (0 = not set)
  int infinite;     // Infinite analysis mode
  int ponder;       // Pondering enabled
  Move ponder_move; // Move to ponder on
} TimeControl;

// Time management result
typedef struct {
  int allocated_time; // Time allocated for this move (ms)
  int max_time;       // Maximum time we can use (ms)
  int optimal_time;   // Optimal time to use (ms)
  int panic_time;     // Emergency time threshold (ms)
} TimeAllocation;

// MultiPV line result
typedef struct {
  Move pv[MAX_DEPTH]; // Principal variation
  int pv_length;      // Length of PV
  Score score;        // Score for this line
  int depth;          // Search depth
  int seldepth;       // Selective depth
  int nodes;          // Nodes searched
} MultiPVLine;

// UCI extension options
typedef struct {
  int multi_pv;        // Number of PV lines to show (1-10)
  int chess960;        // Chess960/FRC support
  int analyse_mode;    // Analysis mode (no time pressure)
  int show_wdl;        // Show Win/Draw/Loss statistics
  int show_currline;   // Show current search line
  int show_currmove;   // Show currently searching move
  int show_refutation; // Show refutation lines
} UCIExtOptions;

// Win/Draw/Loss statistics
typedef struct {
  int win_chance;  // Win probability (per mille)
  int draw_chance; // Draw probability (per mille)
  int loss_chance; // Loss probability (per mille)
} WDLStats;

typedef struct {
  Position position;
  SearchState *search;
  OpeningBook *book;
  int use_book;
  int debug;

  // Time management
  int move_overhead; // Time to reserve for move transmission (ms)
  int last_score;    // Score from last search (for time adjustment)
  int score_drop;    // Flag if score dropped significantly

  // Pondering support
  int pondering;      // Currently pondering
  Move ponder_move;   // Expected opponent move
  Move ponder_result; // Best response found during ponder

  // UCI Extensions
  UCIExtOptions uci_options;         // Extended UCI options
  Move searchmoves[MAX_SEARCHMOVES]; // Restricted move list for go searchmoves
  int num_searchmoves;               // Number of restricted moves
  MultiPVLine multipv_lines[MAX_MULTIPV]; // MultiPV results

  // Game history for 3-fold repetition
  uint64_t game_history[MAX_GAME_MOVES];
  int game_history_count;

} EngineState;

// Time management functions
TimeAllocation allocate_time(const TimeControl *tc, const Position *pos,
                             int last_score);
int get_game_phase_for_time(const Position *pos);

// WDL calculation
WDLStats calculate_wdl(Score score, int ply);

// MultiPV search
void search_multipv(EngineState *engine, int depth, int time_ms);

EngineState *engine_init();
void engine_cleanup(EngineState *engine);

void engine_handle_uci_command(EngineState *engine, const char *line);
void engine_handle_isready_command(EngineState *engine);
void engine_handle_position_command(EngineState *engine, const char *line);
void engine_handle_go_command(EngineState *engine, const char *line);
void engine_handle_setoption_command(EngineState *engine, const char *line);

void engine_run_uci_loop();

void move_to_string(Move move, char *str);

#endif // UCI_H
