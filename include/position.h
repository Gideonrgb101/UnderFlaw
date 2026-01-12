#ifndef POSITION_H
#define POSITION_H

#include "bitboard.h"
#include "magic.h"
#include "types.h"
#include <stdint.h>

#define MAX_PLY 128
#define MAX_GAME_MOVES 1024

typedef struct {
  Bitboard pieces[2][6]; // Pieces for each color and type
  Bitboard occupied[2];  // Occupied squares for each color
  Bitboard all;          // All occupied squares

  int to_move;   // Side to move (WHITE or BLACK)
  int castling;  // Castling rights (KQkq)
  int enpassant; // En passant target square (-1 if none)
  int halfmove;  // Halfmove clock for 50-move rule
  int fullmove;  // Fullmove number

  uint8_t castling_rooks[4]; // Square of the rook for each castling right
                             // (0=WK, 1=WQ, 2=BK, 3=BQ)

  uint64_t zobrist;   // Zobrist hash of current position
  uint64_t pawn_hash; // Pawn structure hash

  Accumulator accumulator; // NNUE Accumulator
} Position;

typedef struct {
  Move move;
  int captured;
  int moving_piece; // The piece type that was moved
  int castling;
  int enpassant;
  int halfmove;
  uint64_t zobrist;
  uint64_t pawn_hash;
  Accumulator accumulator;
} UndoInfo;

// Initialization
void position_init(Position *pos);
void position_from_fen(Position *pos, const char *fen);
void position_to_fen(const Position *pos, char *fen, int max_len);

// Piece manipulation
int position_piece_at(const Position *pos, int sq);
void position_set_piece(Position *pos, int piece, int color, int sq);
void position_remove_piece(Position *pos, int piece, int color, int sq);
void position_move_piece(Position *pos, int piece, int color, int from, int to);

// Move application
void position_make_move(Position *pos, Move move, UndoInfo *undo);
void position_unmake_move(Position *pos, Move move, const UndoInfo *undo);

// Queries
int position_in_check(const Position *pos);
int position_piece_count(const Position *pos, int piece, int color);
int position_material_count(const Position *pos, int color);

// Zobrist hashing
extern uint64_t zobrist_piece[2][6][64];
extern uint64_t zobrist_castle[16];
extern uint64_t zobrist_enpassant[8];
extern uint64_t zobrist_to_move;

void zobrist_init();
uint64_t position_hash(const Position *pos);
uint64_t position_pawn_hash(const Position *pos);

#endif // POSITION_H
