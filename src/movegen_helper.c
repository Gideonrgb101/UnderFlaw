
int movegen_is_pseudo_legal(const Position *pos, Move move) {
  int from = MOVE_FROM(move);
  int to = MOVE_TO(move);
  int color = pos->to_move;

  // Check if from square holds a piece of our color
  int piece = -1;
  for (int p = 0; p < 6; p++) {
    if (pos->pieces[color][p] & (1ULL << from)) {
      piece = p;
      break;
    }
  }

  if (piece == -1) // No piece or wrong color
    return 0;

  // Check destination (cannot capture own piece)
  if (pos->occupied[color] & (1ULL << to))
    return 0;

  Bitboard targets = 0;

  switch (piece) {
  case PAWN: {
    // Pawn logic is complex (push, double push, capture, enpassant)
    Bitboard pawn_bb = (1ULL << from);
    Bitboard empty = ~pos->all;
    Bitboard enemies = pos->occupied[color ^ 1];

    if (color == WHITE) {
      if ((pawn_bb << 8) & empty & (1ULL << to))
        return 1; // Push
      if (SQ_RANK(from) == 1 &&
          (pawn_bb << 16) & empty & ((empty & RANK_3) << 8) & (1ULL << to))
        return 1; // Double push
      if ((pawn_bb << 7) & ~FILE_H & enemies & (1ULL << to))
        return 1; // Capture Left
      if ((pawn_bb << 9) & ~FILE_A & enemies & (1ULL << to))
        return 1; // Capture Right
      // Enpassant
      if (pos->enpassant == to) {
        if ((pawn_bb << 7) & ~FILE_H & (1ULL << to))
          return 1;
        if ((pawn_bb << 9) & ~FILE_A & (1ULL << to))
          return 1;
      }
    } else {
      if ((pawn_bb >> 8) & empty & (1ULL << to))
        return 1;
      if (SQ_RANK(from) == 6 &&
          (pawn_bb >> 16) & empty & ((empty & RANK_6) >> 8) & (1ULL << to))
        return 1;
      if ((pawn_bb >> 7) & ~FILE_A & enemies & (1ULL << to))
        return 1;
      if ((pawn_bb >> 9) & ~FILE_H & enemies & (1ULL << to))
        return 1;
      if (pos->enpassant == to) {
        if ((pawn_bb >> 7) & ~FILE_A & (1ULL << to))
          return 1;
        if ((pawn_bb >> 9) & ~FILE_H & (1ULL << to))
          return 1;
      }
    }
    return 0;
  }
  case KNIGHT:
    targets = knight_attacks[from];
    break;
  case BISHOP:
    targets = get_bishop_attacks(from, pos->all);
    break;
  case ROOK:
    targets = get_rook_attacks(from, pos->all);
    break;
  case QUEEN:
    targets = get_queen_attacks(from, pos->all);
    break;
  case KING:
    targets = king_attacks[from];
    break;
  }

  // Check if target is in attack set
  return (targets & (1ULL << to)) ? 1 : 0;
}
