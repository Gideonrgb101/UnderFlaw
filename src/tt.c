#include "tt.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// MAIN TRANSPOSITION TABLE (Multi-Bucket Design)
// ============================================================================

TranspositionTable *tt_create(int size_mb)
{
    TranspositionTable *tt = (TranspositionTable *)malloc(sizeof(TranspositionTable));
    if (!tt)
        return NULL;

    // Calculate number of clusters (each cluster has TT_CLUSTER_SIZE entries)
    uint64_t bytes = (uint64_t)size_mb * 1024 * 1024;
    tt->num_clusters = bytes / sizeof(TTCluster);

    // Ensure power of 2 for fast modulo via bitwise AND
    uint64_t power = 1;
    while (power * 2 <= tt->num_clusters)
        power *= 2;
    tt->num_clusters = power;

    tt->clusters = (TTCluster *)calloc(tt->num_clusters, sizeof(TTCluster));
    if (!tt->clusters)
    {
        free(tt);
        return NULL;
    }

    tt->count = 0;
    tt->generation = 0;

    return tt;
}

void tt_delete(TranspositionTable *tt)
{
    if (tt)
    {
        free(tt->clusters);
        free(tt);
    }
}

void tt_clear(TranspositionTable *tt)
{
    if (tt && tt->clusters)
    {
        memset(tt->clusters, 0, tt->num_clusters * sizeof(TTCluster));
        tt->count = 0;
        tt->generation = 0;
    }
}

void tt_new_search(TranspositionTable *tt)
{
    if (tt)
    {
        tt->generation++;
        // Wrap around at 255 to fit in uint8_t
        if (tt->generation == 0)
            tt->generation = 1;
    }
}

// Calculate replacement value for an entry
// Higher value = more valuable entry (less likely to be replaced)
static int replacement_value(const TTEntry *entry, uint8_t current_gen)
{
    if (entry->key == 0)
        return -1000; // Empty slot - always replace

    int value = 0;

    // Depth is most important (deeper searches are more valuable)
    value += entry->depth * 4;

    // Exact entries are more valuable than bounds
    if (entry->flag == TT_FLAG_EXACT)
        value += 16;

    // Age penalty: older entries are less valuable
    int age = (int)current_gen - (int)entry->generation;
    if (age < 0)
        age += 256; // Handle wrap-around
    value -= age * 2;

    return value;
}

void tt_store(TranspositionTable *tt, uint64_t key, Score score, int depth, int flag)
{
    tt_store_with_move(tt, key, score, MOVE_NONE, depth, flag);
}

void tt_store_with_move(TranspositionTable *tt, uint64_t key, Score score, Move best_move, int depth, int flag)
{
    if (!tt || !tt->clusters)
        return;

    uint64_t index = key & (tt->num_clusters - 1); // Fast modulo for power of 2
    TTCluster *cluster = &tt->clusters[index];

    TTEntry *replace_entry = NULL;
    int replace_value_score = 1000000; // Start high, find minimum

    // First pass: look for exact key match or find worst entry
    for (int i = 0; i < TT_CLUSTER_SIZE; i++)
    {
        TTEntry *entry = &cluster->entries[i];

        // If we find the same position, update it
        if (entry->key == key)
        {
            // Always update if new depth is >= old depth
            // Or if new entry is exact and old is not
            if (depth >= entry->depth ||
                (flag == TT_FLAG_EXACT && entry->flag != TT_FLAG_EXACT))
            {
                // Preserve best move if new one is MOVE_NONE
                if (best_move == MOVE_NONE && entry->best_move != MOVE_NONE)
                    best_move = entry->best_move;

                entry->key = key;
                entry->score = score;
                entry->best_move = best_move;
                entry->depth = depth;
                entry->flag = flag;
                entry->generation = tt->generation;
            }
            return;
        }

        // Track worst entry for potential replacement
        int value = replacement_value(entry, tt->generation);
        if (value < replace_value_score)
        {
            replace_value_score = value;
            replace_entry = entry;
        }
    }

    // No exact match found - replace worst entry
    if (replace_entry)
    {
        // Special case: don't replace a deep exact entry with a shallow non-exact
        if (replace_entry->flag == TT_FLAG_EXACT &&
            flag != TT_FLAG_EXACT &&
            replace_entry->depth > depth + 3 &&
            replace_entry->generation == tt->generation)
        {
            // Keep the valuable exact entry - find second worst
            int second_worst_value = 1000000;
            TTEntry *second_worst = NULL;

            for (int i = 0; i < TT_CLUSTER_SIZE; i++)
            {
                TTEntry *entry = &cluster->entries[i];
                if (entry == replace_entry)
                    continue;

                int value = replacement_value(entry, tt->generation);
                if (value < second_worst_value)
                {
                    second_worst_value = value;
                    second_worst = entry;
                }
            }

            if (second_worst)
                replace_entry = second_worst;
        }

        if (replace_entry->key == 0)
            tt->count++;

        replace_entry->key = key;
        replace_entry->score = score;
        replace_entry->best_move = best_move;
        replace_entry->depth = depth;
        replace_entry->flag = flag;
        replace_entry->generation = tt->generation;
    }
}

int tt_lookup(TranspositionTable *tt, uint64_t key, Score *score, int depth, int *flag)
{
    if (!tt || !tt->clusters)
        return 0;

    uint64_t index = key & (tt->num_clusters - 1);
    TTCluster *cluster = &tt->clusters[index];

    for (int i = 0; i < TT_CLUSTER_SIZE; i++)
    {
        TTEntry *entry = &cluster->entries[i];

        if (entry->key == key)
        {
            // Update generation on access (keep entry fresh)
            entry->generation = tt->generation;

            if (entry->depth >= depth)
            {
                *score = entry->score;
                *flag = entry->flag;
                return 1;
            }

            // Even if depth is insufficient, we can still return the flag
            // for move ordering purposes
            *flag = entry->flag;
            return 0;
        }
    }

    return 0;
}

Move tt_get_best_move(TranspositionTable *tt, uint64_t key)
{
    if (!tt || !tt->clusters)
        return MOVE_NONE;

    uint64_t index = key & (tt->num_clusters - 1);
    TTCluster *cluster = &tt->clusters[index];

    for (int i = 0; i < TT_CLUSTER_SIZE; i++)
    {
        TTEntry *entry = &cluster->entries[i];

        if (entry->key == key)
        {
            return entry->best_move;
        }
    }

    return MOVE_NONE;
}

int tt_usage(TranspositionTable *tt)
{
    if (!tt || !tt->clusters)
        return 0;

    uint64_t total_slots = tt->num_clusters * TT_CLUSTER_SIZE;
    return (int)((tt->count * 1000) / total_slots);
}

int tt_hashfull(TranspositionTable *tt)
{
    if (!tt || !tt->clusters)
        return 0;

    // Sample first 1000 clusters to estimate fill rate
    int sample_size = (tt->num_clusters < 1000) ? (int)tt->num_clusters : 1000;
    int filled = 0;

    for (int c = 0; c < sample_size; c++)
    {
        for (int i = 0; i < TT_CLUSTER_SIZE; i++)
        {
            if (tt->clusters[c].entries[i].key != 0)
                filled++;
        }
    }
    return (filled * 1000) / (sample_size * TT_CLUSTER_SIZE);
}

// Prefetch TT entry for upcoming position
void tt_prefetch_entry(TranspositionTable *tt, uint64_t key)
{
    if (!tt || !tt->clusters)
        return;

    uint64_t index = key & (tt->num_clusters - 1);
    TTCluster *cluster = &tt->clusters[index];

// Use compiler-specific prefetch intrinsics
#ifdef _MSC_VER
#include <intrin.h>
    _mm_prefetch((const char *)cluster, _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(cluster, 0, 3); // Read, high temporal locality
#endif

// If cluster is large, prefetch second cache line too
// TTCluster with 4 entries is typically >64 bytes
#ifdef _MSC_VER
    _mm_prefetch((const char *)cluster + 64, _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch((char *)cluster + 64, 0, 3);
#endif
}

// ============================================================================
// PAWN HASH TABLE
// ============================================================================

PawnHashTable *pawn_tt_create(int size_kb)
{
    PawnHashTable *pht = (PawnHashTable *)malloc(sizeof(PawnHashTable));
    if (!pht)
        return NULL;

    uint64_t bytes = (uint64_t)size_kb * 1024;
    pht->size = bytes / sizeof(PawnEntry);

    // Ensure power of 2
    uint64_t power = 1;
    while (power * 2 <= pht->size)
        power *= 2;
    pht->size = power;

    pht->entries = (PawnEntry *)calloc(pht->size, sizeof(PawnEntry));
    if (!pht->entries)
    {
        free(pht);
        return NULL;
    }

    pht->hits = 0;
    pht->probes = 0;

    return pht;
}

void pawn_tt_delete(PawnHashTable *pht)
{
    if (pht)
    {
        free(pht->entries);
        free(pht);
    }
}

void pawn_tt_clear(PawnHashTable *pht)
{
    if (pht && pht->entries)
    {
        memset(pht->entries, 0, pht->size * sizeof(PawnEntry));
        pht->hits = 0;
        pht->probes = 0;
    }
}

void pawn_tt_store(PawnHashTable *pht, uint64_t key, int score_mg, int score_eg,
                   int passed_w, int passed_b, int islands_w, int islands_b,
                   uint8_t semi_open_w, uint8_t semi_open_b, uint8_t open_files)
{
    if (!pht || !pht->entries)
        return;

    uint64_t index = key & (pht->size - 1);
    PawnEntry *entry = &pht->entries[index];

    entry->key = key;
    entry->score_mg = (int16_t)score_mg;
    entry->score_eg = (int16_t)score_eg;
    entry->passed_pawns[WHITE] = (int8_t)passed_w;
    entry->passed_pawns[BLACK] = (int8_t)passed_b;
    entry->pawn_islands[WHITE] = (int8_t)islands_w;
    entry->pawn_islands[BLACK] = (int8_t)islands_b;
    entry->semi_open_files[WHITE] = semi_open_w;
    entry->semi_open_files[BLACK] = semi_open_b;
    entry->open_files = open_files;
}

PawnEntry *pawn_tt_probe(PawnHashTable *pht, uint64_t key)
{
    if (!pht || !pht->entries)
        return NULL;

    pht->probes++;

    uint64_t index = key & (pht->size - 1);
    PawnEntry *entry = &pht->entries[index];

    if (entry->key == key)
    {
        pht->hits++;
        return entry;
    }

    return NULL;
}

// ============================================================================
// EVAL HASH TABLE
// ============================================================================

EvalHashTable *eval_tt_create(int size_kb)
{
    EvalHashTable *eht = (EvalHashTable *)malloc(sizeof(EvalHashTable));
    if (!eht)
        return NULL;

    uint64_t bytes = (uint64_t)size_kb * 1024;
    eht->size = bytes / sizeof(EvalEntry);

    // Ensure power of 2
    uint64_t power = 1;
    while (power * 2 <= eht->size)
        power *= 2;
    eht->size = power;

    eht->entries = (EvalEntry *)calloc(eht->size, sizeof(EvalEntry));
    if (!eht->entries)
    {
        free(eht);
        return NULL;
    }

    eht->hits = 0;
    eht->probes = 0;

    return eht;
}

void eval_tt_delete(EvalHashTable *eht)
{
    if (eht)
    {
        free(eht->entries);
        free(eht);
    }
}

void eval_tt_clear(EvalHashTable *eht)
{
    if (eht && eht->entries)
    {
        memset(eht->entries, 0, eht->size * sizeof(EvalEntry));
        eht->hits = 0;
        eht->probes = 0;
    }
}

void eval_tt_store(EvalHashTable *eht, uint64_t key, int score, int game_phase)
{
    if (!eht || !eht->entries)
        return;

    uint64_t index = key & (eht->size - 1);
    EvalEntry *entry = &eht->entries[index];

    entry->key = key;
    entry->score = (int16_t)score;
    entry->game_phase = (int16_t)game_phase;
}

int eval_tt_probe(EvalHashTable *eht, uint64_t key, int *score, int *game_phase)
{
    if (!eht || !eht->entries)
        return 0;

    eht->probes++;

    uint64_t index = key & (eht->size - 1);
    EvalEntry *entry = &eht->entries[index];

    if (entry->key == key)
    {
        eht->hits++;
        *score = entry->score;
        *game_phase = entry->game_phase;
        return 1;
    }

    return 0;
}
