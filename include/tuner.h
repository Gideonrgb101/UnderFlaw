#ifndef TUNER_H
#define TUNER_H

#include "position.h"
#include "types.h"
#include <stdbool.h>

// ===== TUNABLE PARAMETERS =====

// Maximum parameters to tune
#define MAX_PARAMS 128

// Parameter types for organization
typedef enum
{
    PARAM_PIECE_VALUE,    // Material values
    PARAM_PST,            // Piece-square table values
    PARAM_PAWN_STRUCTURE, // Pawn structure bonuses/penalties
    PARAM_MOBILITY,       // Mobility weights
    PARAM_KING_SAFETY,    // King safety terms
    PARAM_PASSED_PAWN,    // Passed pawn evaluation
    PARAM_SEARCH,         // Search parameters (LMR, pruning margins)
    PARAM_OTHER           // Miscellaneous
} ParamType;

// Single tunable parameter
typedef struct
{
    const char *name; // Parameter name for display
    int *value;       // Pointer to actual parameter
    int initial;      // Initial value
    int min_val;      // Minimum allowed value
    int max_val;      // Maximum allowed value
    ParamType type;   // Category
    bool active;      // Whether to tune this parameter
} TunableParam;

// ===== EVALUATION PARAMETERS STRUCTURE =====

typedef struct
{
    // Material values (middlegame, endgame)
    int pawn_mg, pawn_eg;
    int knight_mg, knight_eg;
    int bishop_mg, bishop_eg;
    int rook_mg, rook_eg;
    int queen_mg, queen_eg;

    // Piece-specific bonuses
    int knight_outpost_mg, knight_outpost_eg;
    int knight_mobility_mg, knight_mobility_eg;
    int bishop_pair_mg, bishop_pair_eg;
    int bishop_long_diag_mg, bishop_long_diag_eg;
    int bad_bishop_mg, bad_bishop_eg;
    int rook_open_file_mg, rook_open_file_eg;
    int rook_semi_open_mg, rook_semi_open_eg;
    int rook_7th_rank_mg, rook_7th_rank_eg;
    int rook_connected_mg, rook_connected_eg;
    int queen_mobility_mg, queen_mobility_eg;
    int queen_early_dev_mg, queen_early_dev_eg;

    // Pawn structure
    int doubled_pawn_mg, doubled_pawn_eg;
    int isolated_pawn_mg, isolated_pawn_eg;
    int backward_pawn_mg, backward_pawn_eg;
    int hanging_pawn_mg, hanging_pawn_eg;
    int pawn_chain_mg, pawn_chain_eg;
    int passed_pawn_base_mg, passed_pawn_base_eg;
    int protected_passed_mg, protected_passed_eg;
    int outside_passed_mg, outside_passed_eg;
    int candidate_passed_mg, candidate_passed_eg;
    int pawn_island_mg, pawn_island_eg;

    // King safety
    int king_shelter_mg, king_shelter_eg;
    int king_open_file_mg, king_open_file_eg;
    int pawn_storm_mg, pawn_storm_eg;

    // Positional
    int center_control_mg, center_control_eg;
    int space_bonus_mg, space_bonus_eg;
    int development_mg, development_eg;
    int piece_coord_mg, piece_coord_eg;

    // Tempo
    int tempo_mg, tempo_eg;

} EvalParams;

// ===== SEARCH PARAMETERS STRUCTURE =====

typedef struct
{
    // LMR parameters
    int lmr_base;      // Base constant in LMR formula (scaled by 100)
    int lmr_divisor;   // Divisor in LMR formula (scaled by 100)
    int lmr_min_depth; // Minimum depth for LMR
    int lmr_min_moves; // Minimum move count for LMR

    // Null move pruning
    int nmp_base_reduction; // Base reduction for null move
    int nmp_depth_divisor;  // Depth divisor for adaptive reduction
    int nmp_min_depth;      // Minimum depth for NMP

    // Late move pruning
    int lmp_base;       // Base move count for LMP
    int lmp_multiplier; // Multiplier per depth

    // Futility pruning
    int futility_margin; // Base futility margin (centipawns)
    int futility_depth;  // Maximum depth for futility pruning

    // Reverse futility pruning
    int rfp_margin; // Reverse futility margin (centipawns)
    int rfp_depth;  // Maximum depth for RFP

    // Razoring
    int razor_margin; // Razoring margin (centipawns)
    int razor_depth;  // Maximum depth for razoring

    // SEE pruning
    int see_quiet_margin;   // SEE margin for quiet moves
    int see_capture_margin; // SEE margin for captures

    // Aspiration windows
    int asp_initial_window; // Initial aspiration window size
    int asp_delta;          // Window expansion delta

    // Extensions
    int singular_margin; // Margin for singular extensions

} SearchParams;

// ===== COMBINED PARAMETERS =====

typedef struct
{
    EvalParams eval;
    SearchParams search;
    double fitness; // Fitness score (higher = better)
} TunerParams;

// ===== POSITION DATASET =====

// Position entry with known result
typedef struct
{
    char fen[128]; // FEN string
    double result; // Game result: 1.0 = white win, 0.5 = draw, 0.0 = black win
    int eval;      // Cached evaluation (optional)
} PositionEntry;

// Dataset of positions for tuning
typedef struct
{
    PositionEntry *positions;
    int size;
    int capacity;
} PositionDataset;

// ===== GENETIC ALGORITHM TUNER =====

typedef struct
{
    TunerParams *population;
    int population_size;
    int generations;
    double mutation_rate;
    double crossover_rate;
    int tournament_size;
    int elitism_count;
    bool verbose;
} GeneticTuner;

// Create genetic tuner
GeneticTuner *genetic_tuner_create(int population_size, int generations);

// Destroy genetic tuner
void genetic_tuner_destroy(GeneticTuner *tuner);

// Run genetic algorithm tuning
// Returns best parameters found
TunerParams genetic_tune(GeneticTuner *tuner, int games_per_eval);

// ===== TEXEL TUNER (Gradient Descent) =====

typedef struct
{
    TunerParams params;
    PositionDataset *dataset;
    double learning_rate;
    int max_iterations;
    double k_factor; // Sigmoid scaling factor
    bool verbose;
} TexelTuner;

// Create Texel tuner
TexelTuner *texel_tuner_create(void);

// Destroy Texel tuner
void texel_tuner_destroy(TexelTuner *tuner);

// Load position dataset from PGN file
bool texel_load_pgn(TexelTuner *tuner, const char *filename);

// Load position dataset from EPD file (FEN with results)
bool texel_load_epd(TexelTuner *tuner, const char *filename);

// Add a single position to dataset
void texel_add_position(TexelTuner *tuner, const char *fen, double result);

// Run Texel tuning
// Returns mean squared error of final parameters
double texel_tune(TexelTuner *tuner);

// Compute optimal K factor for sigmoid
double texel_find_k(TexelTuner *tuner);

// ===== SELF-PLAY EVALUATION =====

typedef struct
{
    int wins;
    int losses;
    int draws;
    int total_games;
    double elo_diff;
} MatchResult;

// Play a match between two parameter sets
MatchResult play_match(const TunerParams *params1, const TunerParams *params2,
                       int games, int time_ms, int depth);

// Play a single game and return result (1.0, 0.5, 0.0 from params1's perspective)
double play_game(const TunerParams *white_params, const TunerParams *black_params,
                 int time_ms, int depth);

// ===== PARAMETER I/O =====

// Initialize parameters to default values
void params_init_default(TunerParams *params);

// Copy parameters
void params_copy(TunerParams *dest, const TunerParams *src);

// Save parameters to file
bool params_save(const TunerParams *params, const char *filename);

// Load parameters from file
bool params_load(TunerParams *params, const char *filename);

// Print parameters
void params_print(const TunerParams *params);

// Apply parameters to engine globals
void params_apply(const TunerParams *params);

// ===== DATASET I/O =====

// Create empty dataset
PositionDataset *dataset_create(int initial_capacity);

// Destroy dataset
void dataset_destroy(PositionDataset *dataset);

// Add position to dataset
void dataset_add(PositionDataset *dataset, const char *fen, double result);

// Save dataset to file
bool dataset_save(const PositionDataset *dataset, const char *filename);

// Load dataset from file
bool dataset_load(PositionDataset *dataset, const char *filename);

// Shuffle dataset (for training)
void dataset_shuffle(PositionDataset *dataset);

// ===== UCI INTERFACE FOR TUNING =====

// Start tuning from UCI (for interactive tuning)
void uci_start_tuning(const char *method, const char *datafile);

#endif // TUNER_H
