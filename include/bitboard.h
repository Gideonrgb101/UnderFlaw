#ifndef BITBOARD_H
#define BITBOARD_H

#include <stdint.h>
#include <string.h>

typedef uint64_t Bitboard;

// Ranks and files
#define RANK_1 0x00000000000000FFULL
#define RANK_2 0x000000000000FF00ULL
#define RANK_3 0x0000000000FF0000ULL
#define RANK_4 0x00000000FF000000ULL
#define RANK_5 0x000000FF00000000ULL
#define RANK_6 0x0000FF0000000000ULL
#define RANK_7 0x00FF000000000000ULL
#define RANK_8 0xFF00000000000000ULL

#define FILE_A 0x0101010101010101ULL
#define FILE_B 0x0202020202020202ULL
#define FILE_C 0x0404040404040404ULL
#define FILE_D 0x0808080808080808ULL
#define FILE_E 0x1010101010101010ULL
#define FILE_F 0x2020202020202020ULL
#define FILE_G 0x4040404040404040ULL
#define FILE_H 0x8080808080808080ULL

// Bit operations
#define BB_SET(bb, sq) ((bb) |= (1ULL << (sq)))
#define BB_CLEAR(bb, sq) ((bb) &= ~(1ULL << (sq)))
#define BB_TEST(bb, sq) (((bb) >> (sq)) & 1)
#define BB_TOGGLE(bb, sq) ((bb) ^= (1ULL << (sq)))

// Square indices
#define SQ(file, rank) ((rank) * 8 + (file))
#define SQ_FILE(sq) ((sq) & 7)
#define SQ_RANK(sq) ((sq) >> 3)

// Bit operations with compiler intrinsics
#if defined(__GNUC__)
#define POPCOUNT(bb) __builtin_popcountll(bb)
#define LSB(bb) __builtin_ctzll(bb)
#define MSB(bb) (63 - __builtin_clzll(bb))
#elif defined(_MSC_VER)
#define POPCOUNT(bb) __popcnt64(bb)
#define LSB(bb) _tzcnt_u64(bb)
#define MSB(bb) (63 - _lzcnt_u64(bb))
#else
static inline int popcount(Bitboard bb)
{
    int count = 0;
    while (bb)
    {
        count += bb & 1;
        bb >>= 1;
    }
    return count;
}
#define POPCOUNT(bb) popcount(bb)

static inline int lsb(Bitboard bb)
{
    int pos = 0;
    while (!(bb & 1))
    {
        bb >>= 1;
        pos++;
    }
    return pos;
}
#define LSB(bb) lsb(bb)

static inline int msb(Bitboard bb)
{
    int pos = 0;
    if (bb == 0)
        return -1;
    while (bb > 1)
    {
        bb >>= 1;
        pos++;
    }
    return pos;
}
#define MSB(bb) msb(bb)
#endif

static inline int pop_lsb(Bitboard *bb)
{
    int pos = LSB(*bb);
    *bb &= *bb - 1;
    return pos;
}

static inline int bit_scan(Bitboard bb)
{
    return LSB(bb);
}

// Bitboard iteration - use with: int sq; BB_FOREACH(sq, bb) { ... }
#define BB_FOREACH(sq, bb)                         \
    for (Bitboard _bb = (bb); _bb; _bb &= _bb - 1) \
        for (sq = LSB(_bb), (void)0; sq >= 0; sq = -1)

#endif // BITBOARD_H
