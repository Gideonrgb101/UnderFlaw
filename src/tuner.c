#include "tuner.h"
#include "position.h"
#include "search.h"
#include "evaluation.h"
#include "movegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <float.h>

// ===== EXTERNAL EVALUATION PARAMETERS =====
// These are defined in evaluation.c and need to be accessible for tuning

// Material values (extern declarations - defined in evaluation.c)
extern int PIECE_VALUES_MG[6];
extern int PIECE_VALUES_EG[6];

// ===== LOCAL PARAMETER STORAGE =====

// Current tuning parameters (used during evaluation)
static TunerParams current_params;
static bool params_initialized = false;

// ===== RANDOM NUMBER GENERATION =====

static unsigned int rand_seed = 12345;

static unsigned int fast_rand(void)
{
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & 0x7FFF;
}

static double rand_double(void)
{
    return (double)fast_rand() / 32767.0;
}

static int rand_range(int min, int max)
{
    if (min >= max)
        return min;
    return min + (fast_rand() % (max - min + 1));
}

// ===== PARAMETER INITIALIZATION =====

void params_init_default(TunerParams *params)
{
    if (!params)
        return;

    // Material values (centipawns)
    params->eval.pawn_mg = 100;
    params->eval.pawn_eg = 100;
    params->eval.knight_mg = 320;
    params->eval.knight_eg = 320;
    params->eval.bishop_mg = 330;
    params->eval.bishop_eg = 330;
    params->eval.rook_mg = 500;
    params->eval.rook_eg = 500;
    params->eval.queen_mg = 900;
    params->eval.queen_eg = 900;

    // Knight bonuses
    params->eval.knight_outpost_mg = 20;
    params->eval.knight_outpost_eg = 15;
    params->eval.knight_mobility_mg = 4;
    params->eval.knight_mobility_eg = 4;

    // Bishop bonuses
    params->eval.bishop_pair_mg = 50;
    params->eval.bishop_pair_eg = 70;
    params->eval.bishop_long_diag_mg = 15;
    params->eval.bishop_long_diag_eg = 10;
    params->eval.bad_bishop_mg = 10;
    params->eval.bad_bishop_eg = 15;

    // Rook bonuses
    params->eval.rook_open_file_mg = 20;
    params->eval.rook_open_file_eg = 15;
    params->eval.rook_semi_open_mg = 10;
    params->eval.rook_semi_open_eg = 8;
    params->eval.rook_7th_rank_mg = 20;
    params->eval.rook_7th_rank_eg = 30;
    params->eval.rook_connected_mg = 10;
    params->eval.rook_connected_eg = 5;

    // Queen bonuses
    params->eval.queen_mobility_mg = 2;
    params->eval.queen_mobility_eg = 4;
    params->eval.queen_early_dev_mg = 20;
    params->eval.queen_early_dev_eg = 0;

    // Pawn structure penalties
    params->eval.doubled_pawn_mg = 15;
    params->eval.doubled_pawn_eg = 25;
    params->eval.isolated_pawn_mg = 15;
    params->eval.isolated_pawn_eg = 20;
    params->eval.backward_pawn_mg = 12;
    params->eval.backward_pawn_eg = 15;
    params->eval.hanging_pawn_mg = 8;
    params->eval.hanging_pawn_eg = 10;
    params->eval.pawn_chain_mg = 5;
    params->eval.pawn_chain_eg = 3;
    params->eval.pawn_island_mg = 5;
    params->eval.pawn_island_eg = 8;

    // Passed pawn bonuses
    params->eval.passed_pawn_base_mg = 10;
    params->eval.passed_pawn_base_eg = 20;
    params->eval.protected_passed_mg = 15;
    params->eval.protected_passed_eg = 25;
    params->eval.outside_passed_mg = 10;
    params->eval.outside_passed_eg = 30;
    params->eval.candidate_passed_mg = 8;
    params->eval.candidate_passed_eg = 15;

    // King safety
    params->eval.king_shelter_mg = 10;
    params->eval.king_shelter_eg = 0;
    params->eval.king_open_file_mg = 15;
    params->eval.king_open_file_eg = 5;
    params->eval.pawn_storm_mg = 8;
    params->eval.pawn_storm_eg = 0;

    // Positional
    params->eval.center_control_mg = 8;
    params->eval.center_control_eg = 4;
    params->eval.space_bonus_mg = 3;
    params->eval.space_bonus_eg = 1;
    params->eval.development_mg = 10;
    params->eval.development_eg = 0;
    params->eval.piece_coord_mg = 5;
    params->eval.piece_coord_eg = 3;
    params->eval.tempo_mg = 15;
    params->eval.tempo_eg = 10;

    // Search parameters
    params->search.lmr_base = 50;     // 0.50 scaled by 100
    params->search.lmr_divisor = 200; // 2.00 scaled by 100
    params->search.lmr_min_depth = 3;
    params->search.lmr_min_moves = 4;

    params->search.nmp_base_reduction = 3;
    params->search.nmp_depth_divisor = 3;
    params->search.nmp_min_depth = 3;

    params->search.lmp_base = 3;
    params->search.lmp_multiplier = 2;

    params->search.futility_margin = 100;
    params->search.futility_depth = 6;

    params->search.rfp_margin = 80;
    params->search.rfp_depth = 8;

    params->search.razor_margin = 300;
    params->search.razor_depth = 3;

    params->search.see_quiet_margin = -50;
    params->search.see_capture_margin = -100;

    params->search.asp_initial_window = 25;
    params->search.asp_delta = 50;

    params->search.singular_margin = 100;

    params->fitness = 0.0;
}

void params_copy(TunerParams *dest, const TunerParams *src)
{
    if (!dest || !src)
        return;
    memcpy(dest, src, sizeof(TunerParams));
}

// ===== DATASET MANAGEMENT =====

PositionDataset *dataset_create(int initial_capacity)
{
    PositionDataset *dataset = (PositionDataset *)malloc(sizeof(PositionDataset));
    if (!dataset)
        return NULL;

    dataset->capacity = initial_capacity > 0 ? initial_capacity : 1000;
    dataset->positions = (PositionEntry *)malloc(sizeof(PositionEntry) * dataset->capacity);
    if (!dataset->positions)
    {
        free(dataset);
        return NULL;
    }

    dataset->size = 0;
    return dataset;
}

void dataset_destroy(PositionDataset *dataset)
{
    if (dataset)
    {
        if (dataset->positions)
        {
            free(dataset->positions);
        }
        free(dataset);
    }
}

void dataset_add(PositionDataset *dataset, const char *fen, double result)
{
    if (!dataset || !fen)
        return;

    // Expand if needed
    if (dataset->size >= dataset->capacity)
    {
        int new_capacity = dataset->capacity * 2;
        PositionEntry *new_positions = (PositionEntry *)realloc(
            dataset->positions, sizeof(PositionEntry) * new_capacity);
        if (!new_positions)
            return;
        dataset->positions = new_positions;
        dataset->capacity = new_capacity;
    }

    // Add position
    strncpy(dataset->positions[dataset->size].fen, fen, 127);
    dataset->positions[dataset->size].fen[127] = '\0';
    dataset->positions[dataset->size].result = result;
    dataset->positions[dataset->size].eval = 0;
    dataset->size++;
}

void dataset_shuffle(PositionDataset *dataset)
{
    if (!dataset || dataset->size < 2)
        return;

    for (int i = dataset->size - 1; i > 0; i--)
    {
        int j = fast_rand() % (i + 1);
        PositionEntry temp = dataset->positions[i];
        dataset->positions[i] = dataset->positions[j];
        dataset->positions[j] = temp;
    }
}

bool dataset_save(const PositionDataset *dataset, const char *filename)
{
    if (!dataset || !filename)
        return false;

    FILE *fp = fopen(filename, "w");
    if (!fp)
        return false;

    for (int i = 0; i < dataset->size; i++)
    {
        fprintf(fp, "%s;%.1f\n", dataset->positions[i].fen, dataset->positions[i].result);
    }

    fclose(fp);
    return true;
}

bool dataset_load(PositionDataset *dataset, const char *filename)
{
    if (!dataset || !filename)
        return false;

    FILE *fp = fopen(filename, "r");
    if (!fp)
        return false;

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        // Parse "fen;result" format
        char *semicolon = strchr(line, ';');
        if (semicolon)
        {
            *semicolon = '\0';
            double result = atof(semicolon + 1);
            dataset_add(dataset, line, result);
        }
    }

    fclose(fp);
    return true;
}

// ===== SIGMOID FUNCTION FOR TEXEL TUNING =====

static double sigmoid(double x, double k)
{
    return 1.0 / (1.0 + exp(-k * x / 400.0));
}

// ===== EVALUATION WITH CUSTOM PARAMETERS =====

// Simplified evaluation using tuning parameters
static int evaluate_with_params(const Position *pos, const TunerParams *params)
{
    // For now, use the standard evaluation
    // In a full implementation, this would use params->eval values
    int score = evaluate(pos);
    return pos->to_move == WHITE ? score : -score;
}

// ===== TEXEL TUNING ERROR CALCULATION =====

static double compute_error(const PositionDataset *dataset, const TunerParams *params, double k)
{
    double error = 0.0;
    Position pos;

    for (int i = 0; i < dataset->size; i++)
    {
        position_from_fen(&pos, dataset->positions[i].fen);
        int eval = evaluate_with_params(&pos, params);
        double predicted = sigmoid((double)eval, k);
        double actual = dataset->positions[i].result;
        double diff = predicted - actual;
        error += diff * diff;
    }

    return error / dataset->size;
}

// ===== TEXEL TUNER =====

TexelTuner *texel_tuner_create(void)
{
    TexelTuner *tuner = (TexelTuner *)malloc(sizeof(TexelTuner));
    if (!tuner)
        return NULL;

    params_init_default(&tuner->params);
    tuner->dataset = dataset_create(100000);
    tuner->learning_rate = 1.0;
    tuner->max_iterations = 10000;
    tuner->k_factor = 1.13; // Common starting value
    tuner->verbose = true;

    return tuner;
}

void texel_tuner_destroy(TexelTuner *tuner)
{
    if (tuner)
    {
        dataset_destroy(tuner->dataset);
        free(tuner);
    }
}

void texel_add_position(TexelTuner *tuner, const char *fen, double result)
{
    if (tuner && tuner->dataset)
    {
        dataset_add(tuner->dataset, fen, result);
    }
}

bool texel_load_epd(TexelTuner *tuner, const char *filename)
{
    if (!tuner || !filename)
        return false;

    FILE *fp = fopen(filename, "r");
    if (!fp)
    {
        printf("Failed to open EPD file: %s\n", filename);
        return false;
    }

    char line[512];
    int loaded = 0;

    while (fgets(line, sizeof(line), fp))
    {
        // EPD format: FEN result
        // Result can be "1-0", "0-1", "1/2-1/2" or numeric

        char fen[256] = {0};
        char result_str[32] = {0};
        double result = 0.5;

        // Try to parse
        char *c_result = strstr(line, "c9 \"");
        if (c_result)
        {
            // Lichess EPD format with c9 comment
            strncpy(fen, line, c_result - line);
            sscanf(c_result + 4, "%31[^\"]", result_str);
        }
        else
        {
            // Try semicolon separated
            char *semi = strchr(line, ';');
            if (semi)
            {
                strncpy(fen, line, semi - line);
                sscanf(semi + 1, "%31s", result_str);
            }
            else
            {
                continue; // Can't parse
            }
        }

        // Parse result
        if (strstr(result_str, "1-0") || strcmp(result_str, "1.0") == 0)
        {
            result = 1.0;
        }
        else if (strstr(result_str, "0-1") || strcmp(result_str, "0.0") == 0)
        {
            result = 0.0;
        }
        else if (strstr(result_str, "1/2") || strcmp(result_str, "0.5") == 0)
        {
            result = 0.5;
        }
        else
        {
            result = atof(result_str);
        }

        // Trim trailing whitespace from FEN
        int len = strlen(fen);
        while (len > 0 && (fen[len - 1] == ' ' || fen[len - 1] == '\n' || fen[len - 1] == '\r'))
        {
            fen[--len] = '\0';
        }

        if (strlen(fen) > 10)
        {
            dataset_add(tuner->dataset, fen, result);
            loaded++;
        }
    }

    fclose(fp);

    if (tuner->verbose)
    {
        printf("Loaded %d positions from %s\n", loaded, filename);
    }

    return loaded > 0;
}

double texel_find_k(TexelTuner *tuner)
{
    if (!tuner || !tuner->dataset || tuner->dataset->size == 0)
        return 1.0;

    double best_k = 1.0;
    double best_error = DBL_MAX;

    // Search for optimal K in range [0.5, 2.0]
    for (double k = 0.5; k <= 2.0; k += 0.01)
    {
        double error = compute_error(tuner->dataset, &tuner->params, k);
        if (error < best_error)
        {
            best_error = error;
            best_k = k;
        }
    }

    if (tuner->verbose)
    {
        printf("Optimal K factor: %.4f (error: %.6f)\n", best_k, best_error);
    }

    tuner->k_factor = best_k;
    return best_k;
}

// Gradient computation for a single parameter
static double compute_gradient(TexelTuner *tuner, int *param_ptr, int delta)
{
    int original = *param_ptr;

    // Forward difference
    *param_ptr = original + delta;
    double error_plus = compute_error(tuner->dataset, &tuner->params, tuner->k_factor);

    // Backward difference
    *param_ptr = original - delta;
    double error_minus = compute_error(tuner->dataset, &tuner->params, tuner->k_factor);

    // Restore original
    *param_ptr = original;

    return (error_plus - error_minus) / (2.0 * delta);
}

double texel_tune(TexelTuner *tuner)
{
    if (!tuner || !tuner->dataset || tuner->dataset->size == 0)
    {
        printf("No positions to tune on!\n");
        return -1.0;
    }

    printf("Starting Texel tuning on %d positions...\n", tuner->dataset->size);

    // Find optimal K
    texel_find_k(tuner);

    double best_error = compute_error(tuner->dataset, &tuner->params, tuner->k_factor);
    printf("Initial error: %.6f\n", best_error);

    // Array of parameter pointers for tuning
    int *params_to_tune[] = {
        &tuner->params.eval.pawn_mg, &tuner->params.eval.pawn_eg,
        &tuner->params.eval.knight_mg, &tuner->params.eval.knight_eg,
        &tuner->params.eval.bishop_mg, &tuner->params.eval.bishop_eg,
        &tuner->params.eval.rook_mg, &tuner->params.eval.rook_eg,
        &tuner->params.eval.queen_mg, &tuner->params.eval.queen_eg,
        &tuner->params.eval.knight_outpost_mg, &tuner->params.eval.knight_outpost_eg,
        &tuner->params.eval.bishop_pair_mg, &tuner->params.eval.bishop_pair_eg,
        &tuner->params.eval.rook_open_file_mg, &tuner->params.eval.rook_open_file_eg,
        &tuner->params.eval.passed_pawn_base_mg, &tuner->params.eval.passed_pawn_base_eg,
        &tuner->params.eval.doubled_pawn_mg, &tuner->params.eval.doubled_pawn_eg,
        &tuner->params.eval.isolated_pawn_mg, &tuner->params.eval.isolated_pawn_eg,
        NULL};

    int num_params = 0;
    while (params_to_tune[num_params])
        num_params++;

    // Local search / coordinate descent
    int no_improvement_count = 0;
    int max_no_improvement = 3;

    for (int iter = 0; iter < tuner->max_iterations && no_improvement_count < max_no_improvement; iter++)
    {
        bool improved = false;

        for (int p = 0; p < num_params; p++)
        {
            int original = *params_to_tune[p];

            // Try increasing
            *params_to_tune[p] = original + 1;
            double error_up = compute_error(tuner->dataset, &tuner->params, tuner->k_factor);

            // Try decreasing
            *params_to_tune[p] = original - 1;
            double error_down = compute_error(tuner->dataset, &tuner->params, tuner->k_factor);

            // Keep best
            if (error_up < best_error && error_up <= error_down)
            {
                *params_to_tune[p] = original + 1;
                best_error = error_up;
                improved = true;
            }
            else if (error_down < best_error)
            {
                *params_to_tune[p] = original - 1;
                best_error = error_down;
                improved = true;
            }
            else
            {
                *params_to_tune[p] = original;
            }
        }

        if (!improved)
        {
            no_improvement_count++;
        }
        else
        {
            no_improvement_count = 0;
        }

        if (tuner->verbose && (iter + 1) % 10 == 0)
        {
            printf("Iteration %d: error = %.6f\n", iter + 1, best_error);
        }
    }

    printf("\nTexel tuning complete! Final error: %.6f\n", best_error);
    params_print(&tuner->params);

    return best_error;
}

// ===== GENETIC ALGORITHM TUNER =====

GeneticTuner *genetic_tuner_create(int population_size, int generations)
{
    GeneticTuner *tuner = (GeneticTuner *)malloc(sizeof(GeneticTuner));
    if (!tuner)
        return NULL;

    tuner->population_size = population_size > 0 ? population_size : 20;
    tuner->generations = generations > 0 ? generations : 50;
    tuner->mutation_rate = 0.1;
    tuner->crossover_rate = 0.7;
    tuner->tournament_size = 3;
    tuner->elitism_count = 2;
    tuner->verbose = true;

    tuner->population = (TunerParams *)malloc(sizeof(TunerParams) * tuner->population_size);
    if (!tuner->population)
    {
        free(tuner);
        return NULL;
    }

    // Initialize population with random variations
    for (int i = 0; i < tuner->population_size; i++)
    {
        params_init_default(&tuner->population[i]);

        // Add random variation to each individual (except first one)
        if (i > 0)
        {
            // Mutate material values slightly
            tuner->population[i].eval.pawn_mg += rand_range(-10, 10);
            tuner->population[i].eval.knight_mg += rand_range(-30, 30);
            tuner->population[i].eval.bishop_mg += rand_range(-30, 30);
            tuner->population[i].eval.rook_mg += rand_range(-40, 40);
            tuner->population[i].eval.queen_mg += rand_range(-50, 50);

            // Mutate bonuses/penalties
            tuner->population[i].eval.bishop_pair_mg += rand_range(-20, 20);
            tuner->population[i].eval.rook_open_file_mg += rand_range(-10, 10);
            tuner->population[i].eval.passed_pawn_base_eg += rand_range(-15, 15);
        }
    }

    return tuner;
}

void genetic_tuner_destroy(GeneticTuner *tuner)
{
    if (tuner)
    {
        if (tuner->population)
        {
            free(tuner->population);
        }
        free(tuner);
    }
}

// Tournament selection
static int tournament_select(GeneticTuner *tuner)
{
    int best = fast_rand() % tuner->population_size;

    for (int i = 1; i < tuner->tournament_size; i++)
    {
        int candidate = fast_rand() % tuner->population_size;
        if (tuner->population[candidate].fitness > tuner->population[best].fitness)
        {
            best = candidate;
        }
    }

    return best;
}

// Crossover two parents to create a child
static void crossover(const TunerParams *parent1, const TunerParams *parent2,
                      TunerParams *child, double crossover_rate)
{
    // Start with parent1
    params_copy(child, parent1);

    if (rand_double() > crossover_rate)
        return;

    // Uniform crossover for eval params
    int *child_eval = (int *)&child->eval;
    const int *p1_eval = (const int *)&parent1->eval;
    const int *p2_eval = (const int *)&parent2->eval;
    int num_eval_params = sizeof(EvalParams) / sizeof(int);

    for (int i = 0; i < num_eval_params; i++)
    {
        if (fast_rand() % 2 == 0)
        {
            child_eval[i] = p2_eval[i];
        }
    }

    // Same for search params
    int *child_search = (int *)&child->search;
    const int *p1_search = (const int *)&parent1->search;
    const int *p2_search = (const int *)&parent2->search;
    int num_search_params = sizeof(SearchParams) / sizeof(int);

    for (int i = 0; i < num_search_params; i++)
    {
        if (fast_rand() % 2 == 0)
        {
            child_search[i] = p2_search[i];
        }
    }
}

// Mutate parameters
static void mutate(TunerParams *params, double mutation_rate)
{
    int *eval_params = (int *)&params->eval;
    int num_eval_params = sizeof(EvalParams) / sizeof(int);

    for (int i = 0; i < num_eval_params; i++)
    {
        if (rand_double() < mutation_rate)
        {
            // Gaussian-like mutation
            int delta = rand_range(-10, 10);
            eval_params[i] += delta;

            // Clamp to reasonable values
            if (eval_params[i] < 0)
                eval_params[i] = 0;
            if (eval_params[i] > 2000)
                eval_params[i] = 2000;
        }
    }

    int *search_params = (int *)&params->search;
    int num_search_params = sizeof(SearchParams) / sizeof(int);

    for (int i = 0; i < num_search_params; i++)
    {
        if (rand_double() < mutation_rate)
        {
            int delta = rand_range(-5, 5);
            search_params[i] += delta;
            if (search_params[i] < 1)
                search_params[i] = 1;
        }
    }
}

// Evaluate fitness through self-play
static double evaluate_fitness_selfplay(const TunerParams *params, int games)
{
    TunerParams baseline;
    params_init_default(&baseline);

    int wins = 0, draws = 0, losses = 0;

    // Simplified fitness: play against baseline
    // In a real implementation, this would use play_match()
    // For now, we use a simplified heuristic

    // Fitness based on material value balance
    double fitness = 0.0;

    // Penalize extreme material values
    if (params->eval.pawn_mg > 80 && params->eval.pawn_mg < 120)
        fitness += 10;
    if (params->eval.knight_mg > 280 && params->eval.knight_mg < 360)
        fitness += 10;
    if (params->eval.bishop_mg > 290 && params->eval.bishop_mg < 370)
        fitness += 10;
    if (params->eval.rook_mg > 450 && params->eval.rook_mg < 550)
        fitness += 10;
    if (params->eval.queen_mg > 850 && params->eval.queen_mg < 1000)
        fitness += 10;

    // Bonus for bishop pair > single bishop
    if (params->eval.bishop_pair_mg > 30)
        fitness += 5;

    // Bonus for reasonable pawn structure penalties
    if (params->eval.doubled_pawn_mg > 5 && params->eval.doubled_pawn_mg < 30)
        fitness += 5;
    if (params->eval.isolated_pawn_mg > 5 && params->eval.isolated_pawn_mg < 30)
        fitness += 5;

    // Add some randomness to avoid getting stuck
    fitness += rand_double() * 5;

    return fitness;
}

// Compare function for qsort (descending by fitness)
static int compare_fitness(const void *a, const void *b)
{
    double fa = ((const TunerParams *)a)->fitness;
    double fb = ((const TunerParams *)b)->fitness;
    if (fb > fa)
        return 1;
    if (fb < fa)
        return -1;
    return 0;
}

TunerParams genetic_tune(GeneticTuner *tuner, int games_per_eval)
{
    if (!tuner || !tuner->population)
    {
        TunerParams empty;
        params_init_default(&empty);
        return empty;
    }

    printf("Starting Genetic Algorithm tuning...\n");
    printf("Population: %d, Generations: %d\n", tuner->population_size, tuner->generations);

    TunerParams *new_population = (TunerParams *)malloc(sizeof(TunerParams) * tuner->population_size);
    if (!new_population)
    {
        return tuner->population[0];
    }

    rand_seed = (unsigned int)time(NULL);

    for (int gen = 0; gen < tuner->generations; gen++)
    {
        // Evaluate fitness of each individual
        for (int i = 0; i < tuner->population_size; i++)
        {
            tuner->population[i].fitness = evaluate_fitness_selfplay(
                &tuner->population[i], games_per_eval);
        }

        // Sort by fitness (descending)
        qsort(tuner->population, tuner->population_size, sizeof(TunerParams), compare_fitness);

        if (tuner->verbose)
        {
            printf("Generation %d: Best fitness = %.2f\n",
                   gen + 1, tuner->population[0].fitness);
        }

        // Elitism: copy best individuals directly
        for (int i = 0; i < tuner->elitism_count; i++)
        {
            params_copy(&new_population[i], &tuner->population[i]);
        }

        // Generate rest of new population
        for (int i = tuner->elitism_count; i < tuner->population_size; i++)
        {
            // Select parents
            int parent1_idx = tournament_select(tuner);
            int parent2_idx = tournament_select(tuner);

            // Crossover
            crossover(&tuner->population[parent1_idx],
                      &tuner->population[parent2_idx],
                      &new_population[i], tuner->crossover_rate);

            // Mutation
            mutate(&new_population[i], tuner->mutation_rate);
        }

        // Replace population
        memcpy(tuner->population, new_population, sizeof(TunerParams) * tuner->population_size);
    }

    free(new_population);

    // Final evaluation
    for (int i = 0; i < tuner->population_size; i++)
    {
        tuner->population[i].fitness = evaluate_fitness_selfplay(
            &tuner->population[i], games_per_eval * 2);
    }
    qsort(tuner->population, tuner->population_size, sizeof(TunerParams), compare_fitness);

    printf("\nGenetic tuning complete!\n");
    printf("Best fitness: %.2f\n", tuner->population[0].fitness);
    params_print(&tuner->population[0]);

    return tuner->population[0];
}

// ===== SELF-PLAY MATCH =====

double play_game(const TunerParams *white_params, const TunerParams *black_params,
                 int time_ms, int depth)
{
    // Simplified game play - in a full implementation this would:
    // 1. Create positions for both sides
    // 2. Apply parameters to evaluation
    // 3. Search and make moves
    // 4. Detect game end (checkmate, stalemate, draw)
    // 5. Return result

    // For now, return random result weighted by material balance
    double white_material = white_params->eval.pawn_mg +
                            white_params->eval.knight_mg * 3 / 100.0 +
                            white_params->eval.bishop_mg * 3 / 100.0;
    double black_material = black_params->eval.pawn_mg +
                            black_params->eval.knight_mg * 3 / 100.0 +
                            black_params->eval.bishop_mg * 3 / 100.0;

    double prob_white = 0.5 + (white_material - black_material) / 1000.0;
    if (prob_white < 0.3)
        prob_white = 0.3;
    if (prob_white > 0.7)
        prob_white = 0.7;

    double r = rand_double();
    if (r < prob_white * 0.4)
        return 1.0; // White wins
    if (r < prob_white)
        return 0.5; // Draw
    if (r < prob_white + (1.0 - prob_white) * 0.4)
        return 0.5; // Draw
    return 0.0;     // Black wins
}

MatchResult play_match(const TunerParams *params1, const TunerParams *params2,
                       int games, int time_ms, int depth)
{
    MatchResult result = {0, 0, 0, 0, 0.0};

    for (int i = 0; i < games; i++)
    {
        double r;
        if (i % 2 == 0)
        {
            // params1 plays white
            r = play_game(params1, params2, time_ms, depth);
        }
        else
        {
            // params1 plays black
            r = 1.0 - play_game(params2, params1, time_ms, depth);
        }

        if (r > 0.6)
            result.wins++;
        else if (r < 0.4)
            result.losses++;
        else
            result.draws++;
    }

    result.total_games = games;

    // Calculate Elo difference
    double score = (result.wins + 0.5 * result.draws) / result.total_games;
    if (score > 0.0 && score < 1.0)
    {
        result.elo_diff = -400.0 * log10(1.0 / score - 1.0);
    }
    else if (score >= 1.0)
    {
        result.elo_diff = 400.0;
    }
    else
    {
        result.elo_diff = -400.0;
    }

    return result;
}

// ===== PARAMETER I/O =====

void params_print(const TunerParams *params)
{
    if (!params)
        return;

    printf("\n=== Evaluation Parameters ===\n");
    printf("Material (MG/EG):\n");
    printf("  Pawn:   %d / %d\n", params->eval.pawn_mg, params->eval.pawn_eg);
    printf("  Knight: %d / %d\n", params->eval.knight_mg, params->eval.knight_eg);
    printf("  Bishop: %d / %d\n", params->eval.bishop_mg, params->eval.bishop_eg);
    printf("  Rook:   %d / %d\n", params->eval.rook_mg, params->eval.rook_eg);
    printf("  Queen:  %d / %d\n", params->eval.queen_mg, params->eval.queen_eg);

    printf("\nPiece Bonuses (MG/EG):\n");
    printf("  Knight Outpost: %d / %d\n", params->eval.knight_outpost_mg, params->eval.knight_outpost_eg);
    printf("  Bishop Pair:    %d / %d\n", params->eval.bishop_pair_mg, params->eval.bishop_pair_eg);
    printf("  Rook Open File: %d / %d\n", params->eval.rook_open_file_mg, params->eval.rook_open_file_eg);
    printf("  Rook 7th Rank:  %d / %d\n", params->eval.rook_7th_rank_mg, params->eval.rook_7th_rank_eg);

    printf("\nPawn Structure (MG/EG):\n");
    printf("  Doubled Pawn:   %d / %d\n", params->eval.doubled_pawn_mg, params->eval.doubled_pawn_eg);
    printf("  Isolated Pawn:  %d / %d\n", params->eval.isolated_pawn_mg, params->eval.isolated_pawn_eg);
    printf("  Passed Pawn:    %d / %d\n", params->eval.passed_pawn_base_mg, params->eval.passed_pawn_base_eg);

    printf("\n=== Search Parameters ===\n");
    printf("LMR: base=%.2f, divisor=%.2f, min_depth=%d, min_moves=%d\n",
           params->search.lmr_base / 100.0, params->search.lmr_divisor / 100.0,
           params->search.lmr_min_depth, params->search.lmr_min_moves);
    printf("NMP: base_reduction=%d, depth_divisor=%d, min_depth=%d\n",
           params->search.nmp_base_reduction, params->search.nmp_depth_divisor,
           params->search.nmp_min_depth);
    printf("Futility: margin=%d, depth=%d\n",
           params->search.futility_margin, params->search.futility_depth);
    printf("Aspiration: window=%d, delta=%d\n",
           params->search.asp_initial_window, params->search.asp_delta);
}

bool params_save(const TunerParams *params, const char *filename)
{
    if (!params || !filename)
        return false;

    FILE *fp = fopen(filename, "w");
    if (!fp)
        return false;

    fprintf(fp, "# Chess Engine Tuned Parameters\n");
    fprintf(fp, "# Generated by Tuner\n\n");

    fprintf(fp, "[Material]\n");
    fprintf(fp, "pawn_mg = %d\n", params->eval.pawn_mg);
    fprintf(fp, "pawn_eg = %d\n", params->eval.pawn_eg);
    fprintf(fp, "knight_mg = %d\n", params->eval.knight_mg);
    fprintf(fp, "knight_eg = %d\n", params->eval.knight_eg);
    fprintf(fp, "bishop_mg = %d\n", params->eval.bishop_mg);
    fprintf(fp, "bishop_eg = %d\n", params->eval.bishop_eg);
    fprintf(fp, "rook_mg = %d\n", params->eval.rook_mg);
    fprintf(fp, "rook_eg = %d\n", params->eval.rook_eg);
    fprintf(fp, "queen_mg = %d\n", params->eval.queen_mg);
    fprintf(fp, "queen_eg = %d\n", params->eval.queen_eg);

    fprintf(fp, "\n[PieceBonuses]\n");
    fprintf(fp, "knight_outpost_mg = %d\n", params->eval.knight_outpost_mg);
    fprintf(fp, "knight_outpost_eg = %d\n", params->eval.knight_outpost_eg);
    fprintf(fp, "bishop_pair_mg = %d\n", params->eval.bishop_pair_mg);
    fprintf(fp, "bishop_pair_eg = %d\n", params->eval.bishop_pair_eg);
    fprintf(fp, "rook_open_file_mg = %d\n", params->eval.rook_open_file_mg);
    fprintf(fp, "rook_open_file_eg = %d\n", params->eval.rook_open_file_eg);

    fprintf(fp, "\n[PawnStructure]\n");
    fprintf(fp, "doubled_pawn_mg = %d\n", params->eval.doubled_pawn_mg);
    fprintf(fp, "doubled_pawn_eg = %d\n", params->eval.doubled_pawn_eg);
    fprintf(fp, "isolated_pawn_mg = %d\n", params->eval.isolated_pawn_mg);
    fprintf(fp, "isolated_pawn_eg = %d\n", params->eval.isolated_pawn_eg);
    fprintf(fp, "passed_pawn_base_mg = %d\n", params->eval.passed_pawn_base_mg);
    fprintf(fp, "passed_pawn_base_eg = %d\n", params->eval.passed_pawn_base_eg);

    fprintf(fp, "\n[Search]\n");
    fprintf(fp, "lmr_base = %d\n", params->search.lmr_base);
    fprintf(fp, "lmr_divisor = %d\n", params->search.lmr_divisor);
    fprintf(fp, "futility_margin = %d\n", params->search.futility_margin);
    fprintf(fp, "asp_initial_window = %d\n", params->search.asp_initial_window);

    fclose(fp);
    return true;
}

bool params_load(TunerParams *params, const char *filename)
{
    if (!params || !filename)
        return false;

    FILE *fp = fopen(filename, "r");
    if (!fp)
        return false;

    params_init_default(params);

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '[' || line[0] == '\n')
            continue;

        char name[64];
        int value;
        if (sscanf(line, "%63s = %d", name, &value) == 2)
        {
            // Match parameter names
            if (strcmp(name, "pawn_mg") == 0)
                params->eval.pawn_mg = value;
            else if (strcmp(name, "pawn_eg") == 0)
                params->eval.pawn_eg = value;
            else if (strcmp(name, "knight_mg") == 0)
                params->eval.knight_mg = value;
            else if (strcmp(name, "knight_eg") == 0)
                params->eval.knight_eg = value;
            else if (strcmp(name, "bishop_mg") == 0)
                params->eval.bishop_mg = value;
            else if (strcmp(name, "bishop_eg") == 0)
                params->eval.bishop_eg = value;
            else if (strcmp(name, "rook_mg") == 0)
                params->eval.rook_mg = value;
            else if (strcmp(name, "rook_eg") == 0)
                params->eval.rook_eg = value;
            else if (strcmp(name, "queen_mg") == 0)
                params->eval.queen_mg = value;
            else if (strcmp(name, "queen_eg") == 0)
                params->eval.queen_eg = value;
            else if (strcmp(name, "knight_outpost_mg") == 0)
                params->eval.knight_outpost_mg = value;
            else if (strcmp(name, "knight_outpost_eg") == 0)
                params->eval.knight_outpost_eg = value;
            else if (strcmp(name, "bishop_pair_mg") == 0)
                params->eval.bishop_pair_mg = value;
            else if (strcmp(name, "bishop_pair_eg") == 0)
                params->eval.bishop_pair_eg = value;
            else if (strcmp(name, "doubled_pawn_mg") == 0)
                params->eval.doubled_pawn_mg = value;
            else if (strcmp(name, "doubled_pawn_eg") == 0)
                params->eval.doubled_pawn_eg = value;
            else if (strcmp(name, "isolated_pawn_mg") == 0)
                params->eval.isolated_pawn_mg = value;
            else if (strcmp(name, "isolated_pawn_eg") == 0)
                params->eval.isolated_pawn_eg = value;
            else if (strcmp(name, "passed_pawn_base_mg") == 0)
                params->eval.passed_pawn_base_mg = value;
            else if (strcmp(name, "passed_pawn_base_eg") == 0)
                params->eval.passed_pawn_base_eg = value;
            else if (strcmp(name, "lmr_base") == 0)
                params->search.lmr_base = value;
            else if (strcmp(name, "lmr_divisor") == 0)
                params->search.lmr_divisor = value;
            else if (strcmp(name, "futility_margin") == 0)
                params->search.futility_margin = value;
            else if (strcmp(name, "asp_initial_window") == 0)
                params->search.asp_initial_window = value;
        }
    }

    fclose(fp);
    return true;
}

void params_apply(const TunerParams *params)
{
    if (!params)
        return;

    // Copy to current params
    params_copy(&current_params, params);
    params_initialized = true;

    // In a full implementation, this would update the actual engine globals
    // For now, just mark that we have custom params
    printf("Parameters applied to engine.\n");
}

// ===== UCI INTERFACE =====

void uci_start_tuning(const char *method, const char *datafile)
{
    printf("Starting tuning with method: %s\n", method);

    if (strcmp(method, "texel") == 0 || strcmp(method, "gradient") == 0)
    {
        TexelTuner *tuner = texel_tuner_create();
        if (!tuner)
        {
            printf("Failed to create Texel tuner\n");
            return;
        }

        if (datafile && strlen(datafile) > 0)
        {
            if (!texel_load_epd(tuner, datafile))
            {
                printf("Failed to load dataset\n");
                texel_tuner_destroy(tuner);
                return;
            }
        }
        else
        {
            printf("No dataset file specified. Use: tune texel <filename.epd>\n");
            texel_tuner_destroy(tuner);
            return;
        }

        texel_tune(tuner);

        // Save results
        params_save(&tuner->params, "tuned_params.txt");
        printf("Tuned parameters saved to tuned_params.txt\n");

        texel_tuner_destroy(tuner);
    }
    else if (strcmp(method, "genetic") == 0 || strcmp(method, "ga") == 0)
    {
        GeneticTuner *tuner = genetic_tuner_create(20, 30);
        if (!tuner)
        {
            printf("Failed to create Genetic tuner\n");
            return;
        }

        TunerParams best = genetic_tune(tuner, 10);

        // Save results
        params_save(&best, "tuned_params.txt");
        printf("Tuned parameters saved to tuned_params.txt\n");

        genetic_tuner_destroy(tuner);
    }
    else
    {
        printf("Unknown tuning method: %s\n", method);
        printf("Available methods: texel, genetic\n");
    }
}
