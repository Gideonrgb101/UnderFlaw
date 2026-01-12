#ifndef CACHE_H
#define CACHE_H

#include "types.h"
#include <stddef.h>
#include <stdint.h>


// ============================================================================
// CACHE AND MEMORY OPTIMIZATION UTILITIES
// ============================================================================

// Cache line size (64 bytes on most modern CPUs)
#define CACHE_LINE_SIZE 64

// ===== COMPILER-SPECIFIC ALIGNMENT MACROS =====

#ifdef _MSC_VER
// Microsoft Visual C++
#define CACHE_ALIGN __declspec(align(64))
#define CACHE_ALIGN_ATTR
#define PREFETCH(addr) _mm_prefetch((const char *)(addr), _MM_HINT_T0)
#define PREFETCH_WRITE(addr) _mm_prefetch((const char *)(addr), _MM_HINT_T0)
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
// GCC and Clang
#define CACHE_ALIGN
#define CACHE_ALIGN_ATTR __attribute__((aligned(64)))
#define PREFETCH(addr) __builtin_prefetch((addr), 0, 3)
#define PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
#else
// Fallback: no prefetch, no alignment
#define CACHE_ALIGN
#define CACHE_ALIGN_ATTR
#define PREFETCH(addr) ((void)(addr))
#define PREFETCH_WRITE(addr) ((void)(addr))
#endif

// ===== STACK-BASED MOVE LIST =====

// Maximum moves in a position (256 is plenty; typical max is ~218)
#define MAX_MOVES 256

// Macro for declaring a stack-allocated move list
// Usage: STACK_MOVELIST(my_moves);
#define STACK_MOVELIST(name)                                                   \
  Move name##_buffer[MAX_MOVES];                                               \
  int name##_count = 0

// Macros for manipulating stack move lists
#define MOVELIST_ADD(name, move)                                               \
  do {                                                                         \
    if (name##_count < MAX_MOVES)                                              \
      name##_buffer[name##_count++] = (move);                                  \
  } while (0)

#define MOVELIST_COUNT(name) (name##_count)
#define MOVELIST_GET(name, idx) (name##_buffer[idx])
#define MOVELIST_CLEAR(name) (name##_count = 0)
#define MOVELIST_BUFFER(name) (name##_buffer)

// ===== PACKED PIECE-SQUARE TABLES =====

// Packed PST entry: combines middlegame and endgame values in single int32
// Upper 16 bits = endgame, Lower 16 bits = middlegame
typedef int32_t PackedScore;

#define PACK_SCORE(mg, eg)                                                     \
  ((PackedScore)(((int32_t)(eg) << 16) + (int32_t)(mg)))
#define UNPACK_MG(s) ((int16_t)((s) & 0xFFFF))
#define UNPACK_EG(s) ((int16_t)((s) >> 16))

// Add two packed scores
#define PACKED_ADD(a, b) ((a) + (b))

// Subtract two packed scores
#define PACKED_SUB(a, b) ((a) - (b))

// Interpolate between MG and EG based on phase (0 = endgame, 256 = opening)
static inline Score interpolate_packed(PackedScore ps, int phase) {
  int mg = UNPACK_MG(ps);
  int eg = UNPACK_EG(ps);
  return (Score)((mg * phase + eg * (256 - phase)) / 256);
}

// ===== CACHE-ALIGNED PST STRUCTURE =====

// Each PST is 64 entries of PackedScore (4 bytes each = 256 bytes = 4 cache
// lines) We align each PST to cache line boundary
typedef struct {
  PackedScore values[64];
} CACHE_ALIGN_ATTR AlignedPST;

// Complete set of PSTs for one color
typedef struct {
  AlignedPST pawn;
  AlignedPST knight;
  AlignedPST bishop;
  AlignedPST rook;
  AlignedPST queen;
  AlignedPST king_mg; // Middlegame king (castled)
  AlignedPST king_eg; // Endgame king (centralized)
} CACHE_ALIGN_ATTR PSTSet;

// Material values packed (MG, EG)
typedef struct {
  PackedScore pawn;
  PackedScore knight;
  PackedScore bishop;
  PackedScore rook;
  PackedScore queen;
} CACHE_ALIGN_ATTR PackedMaterial;

// ===== MEMORY POOL FOR SMALL ALLOCATIONS =====

// Simple fixed-size memory pool to avoid heap allocations during search
#define MEMPOOL_BLOCK_SIZE 4096
#define MEMPOOL_MAX_BLOCKS 64

typedef struct {
  char *blocks[MEMPOOL_MAX_BLOCKS];
  int num_blocks;
  int current_block;
  int offset; // Current offset in current block
} MemPool;

// Initialize memory pool
void mempool_init(MemPool *pool);

// Allocate from pool (returns NULL if pool exhausted)
void *mempool_alloc(MemPool *pool, size_t size);

// Reset pool (free all allocations but keep memory)
void mempool_reset(MemPool *pool);

// Destroy pool (free all memory)
void mempool_destroy(MemPool *pool);

// ===== BITBOARD OPERATIONS (CACHE-FRIENDLY) =====

// Count bits in multiple bitboards at once (better instruction pipelining)
static inline void popcount_multi(const uint64_t *bbs, int *counts, int n) {
  for (int i = 0; i < n; i++) {
#ifdef _MSC_VER
    counts[i] = (int)__popcnt64(bbs[i]);
#else
    counts[i] = __builtin_popcountll(bbs[i]);
#endif
  }
}

// ===== BRANCH PREDICTION HINTS =====

#ifdef __GNUC__
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

// ===== FORCE INLINE =====

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline)) inline
#else
#define FORCE_INLINE inline
#endif

// ===== PACKED PST INITIALIZATION =====

void init_packed_pst(AlignedPST *pst, const int *mg_table, const int *eg_table);
void init_packed_material(PackedMaterial *mat, int pawn_mg, int pawn_eg,
                          int knight_mg, int knight_eg, int bishop_mg,
                          int bishop_eg, int rook_mg, int rook_eg, int queen_mg,
                          int queen_eg);

// Evaluate pieces using packed PST (returns packed score)
PackedScore evaluate_pieces_packed(const AlignedPST *pst, uint64_t pieces,
                                   int flip);

// Prefetch PST entries for upcoming evaluation
void prefetch_pst_entries(const AlignedPST *pst, uint64_t pieces);

#endif // CACHE_H
