#ifndef TT_H
#define TT_H

#include "types.h"
#include <stdint.h>

// ===== CACHE ALIGNMENT =====
#ifdef _MSC_VER
#define TT_CACHE_ALIGN __declspec(align(64))
#elif defined(__GNUC__) || defined(__clang__)
#define TT_CACHE_ALIGN __attribute__((aligned(64)))
#else
#define TT_CACHE_ALIGN
#endif

// TT Entry flags
#define TT_FLAG_NONE 0
#define TT_FLAG_LOWER 1
#define TT_FLAG_UPPER 2
#define TT_FLAG_EXACT 3

// Number of entries per cluster (bucket)
#define TT_CLUSTER_SIZE 4

// ===== Main Transposition Table =====

typedef struct
{
    uint64_t key;       // Zobrist key (full key for verification)
    Score score;        // Search score
    Move best_move;     // Best move found
    int16_t depth;      // Search depth
    uint8_t flag;       // Node type (EXACT, LOWER, UPPER)
    uint8_t generation; // Age for replacement
} TTEntry;

typedef struct
{
    TTEntry entries[TT_CLUSTER_SIZE]; // Multiple entries per bucket
} TTCluster;

typedef struct
{
    TTCluster *clusters;
    uint64_t num_clusters;
    uint64_t count;
    uint8_t generation; // Current search generation
} TranspositionTable;

// ===== Pawn Hash Table =====

typedef struct
{
    uint64_t key;
    int16_t score_mg;           // Middlegame pawn structure score
    int16_t score_eg;           // Endgame pawn structure score
    int8_t passed_pawns[2];     // Count of passed pawns per side
    int8_t pawn_islands[2];     // Count of pawn islands per side
    uint8_t semi_open_files[2]; // Bitfield of semi-open files per side
    uint8_t open_files;         // Bitfield of open files
} PawnEntry;

typedef struct
{
    PawnEntry *entries;
    uint64_t size;
    uint64_t hits;
    uint64_t probes;
} PawnHashTable;

// ===== Eval Hash Table =====

typedef struct
{
    uint64_t key;
    int16_t score;
    int16_t game_phase;
} EvalEntry;

typedef struct
{
    EvalEntry *entries;
    uint64_t size;
    uint64_t hits;
    uint64_t probes;
} EvalHashTable;

// ===== Main TT Functions =====

TranspositionTable *tt_create(int size_mb);
void tt_delete(TranspositionTable *tt);
void tt_clear(TranspositionTable *tt);
void tt_new_search(TranspositionTable *tt); // Increment generation

void tt_store(TranspositionTable *tt, uint64_t key, Score score, int depth, int flag);
void tt_store_with_move(TranspositionTable *tt, uint64_t key, Score score, Move best_move, int depth, int flag);
int tt_lookup(TranspositionTable *tt, uint64_t key, Score *score, int depth, int *flag);
Move tt_get_best_move(TranspositionTable *tt, uint64_t key);

int tt_usage(TranspositionTable *tt);
int tt_hashfull(TranspositionTable *tt); // Per mille usage for UCI

// Prefetch TT entry (call ahead of time for cache optimization)
void tt_prefetch_entry(TranspositionTable *tt, uint64_t key);

// ===== Pawn Hash Functions =====

PawnHashTable *pawn_tt_create(int size_kb);
void pawn_tt_delete(PawnHashTable *pht);
void pawn_tt_clear(PawnHashTable *pht);

void pawn_tt_store(PawnHashTable *pht, uint64_t key, int score_mg, int score_eg,
                   int passed_w, int passed_b, int islands_w, int islands_b,
                   uint8_t semi_open_w, uint8_t semi_open_b, uint8_t open_files);
PawnEntry *pawn_tt_probe(PawnHashTable *pht, uint64_t key);

// ===== Eval Hash Functions =====

EvalHashTable *eval_tt_create(int size_kb);
void eval_tt_delete(EvalHashTable *eht);
void eval_tt_clear(EvalHashTable *eht);

void eval_tt_store(EvalHashTable *eht, uint64_t key, int score, int game_phase);
int eval_tt_probe(EvalHashTable *eht, uint64_t key, int *score, int *game_phase);

#endif // TT_H
