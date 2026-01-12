#include "cache.h"
#include "tt.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// NOTE: TT prefetching is implemented in tt.c (tt_prefetch_entry function)
// to avoid circular dependencies and keep TT-related code together
// ============================================================================

// ============================================================================
// MEMORY POOL IMPLEMENTATION
// ============================================================================

void mempool_init(MemPool *pool)
{
    pool->num_blocks = 0;
    pool->current_block = -1;
    pool->offset = MEMPOOL_BLOCK_SIZE; // Force allocation on first use

    for (int i = 0; i < MEMPOOL_MAX_BLOCKS; i++)
    {
        pool->blocks[i] = NULL;
    }
}

void *mempool_alloc(MemPool *pool, size_t size)
{
    // Align size to 8 bytes
    size = (size + 7) & ~7;

    // Check if size is too large for a single block
    if (size > MEMPOOL_BLOCK_SIZE)
        return NULL;

    // Check if we need a new block
    if (pool->current_block < 0 || pool->offset + size > MEMPOOL_BLOCK_SIZE)
    {
        // Try to use next existing block
        if (pool->current_block + 1 < pool->num_blocks)
        {
            pool->current_block++;
            pool->offset = 0;
        }
        else
        {
            // Need to allocate new block
            if (pool->num_blocks >= MEMPOOL_MAX_BLOCKS)
                return NULL;

            char *new_block = (char *)malloc(MEMPOOL_BLOCK_SIZE);
            if (!new_block)
                return NULL;

            pool->blocks[pool->num_blocks] = new_block;
            pool->current_block = pool->num_blocks;
            pool->num_blocks++;
            pool->offset = 0;
        }
    }

    void *ptr = pool->blocks[pool->current_block] + pool->offset;
    pool->offset += size;
    return ptr;
}

void mempool_reset(MemPool *pool)
{
    pool->current_block = (pool->num_blocks > 0) ? 0 : -1;
    pool->offset = 0;
}

void mempool_destroy(MemPool *pool)
{
    for (int i = 0; i < pool->num_blocks; i++)
    {
        free(pool->blocks[i]);
        pool->blocks[i] = NULL;
    }
    pool->num_blocks = 0;
    pool->current_block = -1;
    pool->offset = 0;
}

// ============================================================================
// PACKED PIECE-SQUARE TABLE INITIALIZATION
// ============================================================================

// Initialize a packed PST from separate MG and EG tables
void init_packed_pst(AlignedPST *pst, const int *mg_table, const int *eg_table)
{
    for (int sq = 0; sq < 64; sq++)
    {
        pst->values[sq] = PACK_SCORE(mg_table[sq], eg_table[sq]);
    }
}

// Initialize material values
void init_packed_material(PackedMaterial *mat,
                          int pawn_mg, int pawn_eg,
                          int knight_mg, int knight_eg,
                          int bishop_mg, int bishop_eg,
                          int rook_mg, int rook_eg,
                          int queen_mg, int queen_eg)
{
    mat->pawn = PACK_SCORE(pawn_mg, pawn_eg);
    mat->knight = PACK_SCORE(knight_mg, knight_eg);
    mat->bishop = PACK_SCORE(bishop_mg, bishop_eg);
    mat->rook = PACK_SCORE(rook_mg, rook_eg);
    mat->queen = PACK_SCORE(queen_mg, queen_eg);
}

// ============================================================================
// CACHE-FRIENDLY EVALUATION HELPERS
// ============================================================================

// Evaluate all pieces of one type with prefetching
// This evaluates multiple pieces together for better cache behavior
PackedScore evaluate_pieces_packed(const AlignedPST *pst, uint64_t pieces, int flip)
{
    PackedScore total = 0;

    while (pieces)
    {
// Get next piece square
#ifdef _MSC_VER
        unsigned long sq;
        _BitScanForward64(&sq, pieces);
#else
        int sq = __builtin_ctzll(pieces);
#endif

        pieces &= pieces - 1; // Clear LSB

        int idx = flip ? (sq ^ 56) : sq; // Flip for black
        total = PACKED_ADD(total, pst->values[idx]);
    }

    return total;
}

// Prefetch PST entries for a bitboard of pieces
void prefetch_pst_entries(const AlignedPST *pst, uint64_t pieces)
{
    // Prefetch first few piece positions
    int prefetch_count = 0;
    uint64_t bb = pieces;

    while (bb && prefetch_count < 4)
    {
#ifdef _MSC_VER
        unsigned long sq;
        _BitScanForward64(&sq, bb);
#else
        int sq = __builtin_ctzll(bb);
#endif

        bb &= bb - 1;
        PREFETCH(&pst->values[sq]);
        prefetch_count++;
    }
}
