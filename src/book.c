#include "book.h"
#include "movegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ===== RANDOM NUMBER GENERATION =====

static unsigned int book_rand_seed = 12345;

static void book_srand(unsigned int seed) { book_rand_seed = seed; }

static unsigned int book_rand(void) {
  book_rand_seed = book_rand_seed * 1103515245 + 12345;
  return (book_rand_seed >> 16) & 0x7FFF;
}

static int book_rand_range(int max) {
  if (max <= 0)
    return 0;
  return book_rand() % max;
}

// ===== POLYGLOT MOVE DECODING =====

// Polyglot move encoding:
// bits 0-2: to file (0=a, 7=h)
// bits 3-5: to rank (0=1, 7=8)
// bits 6-8: from file
// bits 9-11: from rank
// bits 12-14: promotion piece (0=none, 1=knight, 2=bishop, 3=rook, 4=queen)

static Move decode_poly_move(const Position *pos, uint16_t poly_move) {
  int to_file = poly_move & 7;
  int to_rank = (poly_move >> 3) & 7;
  int from_file = (poly_move >> 6) & 7;
  int from_rank = (poly_move >> 9) & 7;
  int promo = (poly_move >> 12) & 7;

  int from = from_file + from_rank * 8;
  int to = to_file + to_rank * 8;

  // Handle castling (Polyglot uses king target square)
  if (pos->pieces[pos->to_move][KING] & (1ULL << from)) {
    if (from == 4 && to == 6)
      to = 6; // e1-g1 = O-O white
    else if (from == 4 && to == 2)
      to = 2; // e1-c1 = O-O-O white
    else if (from == 60 && to == 62)
      to = 62; // e8-g8 = O-O black
    else if (from == 60 && to == 58)
      to = 58; // e8-c8 = O-O-O black
  }

  // Determine move flag
  int flag = FLAG_QUIET;
  Bitboard target_sq = 1ULL << to;
  if (pos->occupied[pos->to_move ^ 1] & target_sq) {
    flag = FLAG_CAPTURE;
  }

  // Promotion
  int promotion = 0;
  if (promo >= 1 && promo <= 4) {
    promotion = promo;
  }

  // Create move
  Move move = MAKE_MOVE(from, to, promotion, flag);

  // Verify move is legal
  if (movegen_is_legal(pos, move)) {
    return move;
  }

  // Try with capture flag if quiet didn't work
  if (flag == FLAG_QUIET) {
    move = MAKE_MOVE(from, to, promotion, FLAG_CAPTURE);
    if (movegen_is_legal(pos, move)) {
      return move;
    }
  }

  // Try to find the move in legal moves
  MoveList moves;
  movegen_all(pos, &moves);
  for (int i = 0; i < moves.count; i++) {
    Move m = moves.moves[i];
    if (MOVE_FROM(m) == from && MOVE_TO(m) == to) {
      if (movegen_is_legal(pos, m)) {
        return m;
      }
    }
  }

  return MOVE_NONE;
}

// ===== BINARY I/O HELPERS =====

static uint64_t read_uint64_be(FILE *f) {
  unsigned char buf[8];
  if (fread(buf, 1, 8, f) != 8)
    return 0;
  return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) |
         ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
         ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
         ((uint64_t)buf[6] << 8) | ((uint64_t)buf[7]);
}

static uint16_t read_uint16_be(FILE *f) {
  unsigned char buf[2];
  if (fread(buf, 1, 2, f) != 2)
    return 0;
  return ((uint16_t)buf[0] << 8) | ((uint16_t)buf[1]);
}

static uint32_t read_uint32_be(FILE *f) {
  unsigned char buf[4];
  if (fread(buf, 1, 4, f) != 4)
    return 0;
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3]);
}

static void write_uint64_be(FILE *f, uint64_t val) {
  unsigned char buf[8];
  buf[0] = (val >> 56) & 0xFF;
  buf[1] = (val >> 48) & 0xFF;
  buf[2] = (val >> 40) & 0xFF;
  buf[3] = (val >> 32) & 0xFF;
  buf[4] = (val >> 24) & 0xFF;
  buf[5] = (val >> 16) & 0xFF;
  buf[6] = (val >> 8) & 0xFF;
  buf[7] = val & 0xFF;
  fwrite(buf, 1, 8, f);
}

static void write_uint32_be(FILE *f, uint32_t val) {
  unsigned char buf[4];
  buf[0] = (val >> 24) & 0xFF;
  buf[1] = (val >> 16) & 0xFF;
  buf[2] = (val >> 8) & 0xFF;
  buf[3] = val & 0xFF;
  fwrite(buf, 1, 4, f);
}

// ===== BINARY SEARCH FOR POLYGLOT ENTRIES =====

static int find_poly_entry(const OpeningBook *book, uint64_t key) {
  if (!book->poly_entries || book->poly_count == 0)
    return -1;

  int lo = 0;
  int hi = book->poly_count - 1;

  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (book->poly_entries[mid].key < key) {
      lo = mid + 1;
    } else if (book->poly_entries[mid].key > key) {
      hi = mid - 1;
    } else {
      // Found - scan backward to find first entry with this key
      while (mid > 0 && book->poly_entries[mid - 1].key == key) {
        mid--;
      }
      return mid;
    }
  }
  return -1;
}

// ===== HASH TABLE FOR ENHANCED ENTRIES =====

#define BOOK_HASH_SIZE 65536
#define BOOK_HASH_MASK (BOOK_HASH_SIZE - 1)

static int hash_index(uint64_t key) { return (int)(key & BOOK_HASH_MASK); }

static BookEntry *find_book_entry(OpeningBook *book, uint64_t key) {
  if (!book->entries)
    return NULL;

  int idx = hash_index(key);
  int start = idx;

  // Linear probing
  do {
    if (book->entries[idx].key == key && book->entries[idx].move_count > 0) {
      return &book->entries[idx];
    }
    if (book->entries[idx].move_count == 0 && book->entries[idx].key == 0) {
      return NULL; // Empty slot, not found
    }
    idx = (idx + 1) & BOOK_HASH_MASK;
  } while (idx != start);

  return NULL;
}

static BookEntry *find_or_create_entry(OpeningBook *book, uint64_t key) {
  if (!book->entries)
    return NULL;

  int idx = hash_index(key);
  int start = idx;
  int first_empty = -1;

  // Linear probing
  do {
    if (book->entries[idx].key == key && book->entries[idx].move_count > 0) {
      return &book->entries[idx];
    }
    if (book->entries[idx].move_count == 0 && first_empty < 0) {
      first_empty = idx;
    }
    idx = (idx + 1) & BOOK_HASH_MASK;
  } while (idx != start);

  // Create new entry
  if (first_empty >= 0) {
    book->entries[first_empty].key = key;
    book->entries[first_empty].move_count = 0;
    book->entries[first_empty].flags = 0;
    memset(book->entries[first_empty].moves, 0,
           sizeof(book->entries[first_empty].moves));
    memset(book->entries[first_empty].weights, 0,
           sizeof(book->entries[first_empty].weights));
    memset(book->entries[first_empty].learn, 0,
           sizeof(book->entries[first_empty].learn));
    memset(book->entries[first_empty].games, 0,
           sizeof(book->entries[first_empty].games));
    book->num_entries++;
    return &book->entries[first_empty];
  }

  return NULL;
}

// ===== BOOK CREATION/DESTRUCTION =====

OpeningBook *book_create(void) {
  OpeningBook *book = (OpeningBook *)malloc(sizeof(OpeningBook));
  if (!book)
    return NULL;

  book->poly_entries = NULL;
  book->poly_count = 0;

  // Allocate enhanced entries hash table
  book->entries = (BookEntry *)calloc(BOOK_HASH_SIZE, sizeof(BookEntry));
  book->num_entries = 0;
  book->capacity = BOOK_HASH_SIZE;

  book->loaded = false;
  book->learning_enabled = true;
  book->use_weights = true;
  book->random_factor = 0;

  book->probes = 0;
  book->hits = 0;
  book->misses = 0;
  book->learning_updates = 0;

  memset(book->book_file, 0, sizeof(book->book_file));
  memset(book->learn_file, 0, sizeof(book->learn_file));

  // Seed random
  book_srand((unsigned int)time(NULL));

  return book;
}

void book_delete(OpeningBook *book) {
  if (book) {
    if (book->poly_entries) {
      free(book->poly_entries);
    }
    if (book->entries) {
      free(book->entries);
    }
    free(book);
  }
}

// ===== FILE I/O =====

int book_load(OpeningBook *book, const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (!f) {
    return 0;
  }

  // Get file size
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  // Each Polyglot entry is 16 bytes
  int num_entries = (int)(file_size / 16);
  if (num_entries <= 0) {
    fclose(f);
    return 0;
  }

  // Free old entries if any
  if (book->poly_entries) {
    free(book->poly_entries);
  }

  // Allocate entries
  book->poly_entries =
      (PolyglotEntry *)malloc(num_entries * sizeof(PolyglotEntry));
  if (!book->poly_entries) {
    fclose(f);
    return 0;
  }

  // Read entries
  for (int i = 0; i < num_entries; i++) {
    book->poly_entries[i].key = read_uint64_be(f);
    book->poly_entries[i].move = read_uint16_be(f);
    book->poly_entries[i].weight = read_uint16_be(f);
    book->poly_entries[i].learn = read_uint32_be(f);
  }

  book->poly_count = num_entries;
  book->loaded = true;
  strncpy(book->book_file, filename, sizeof(book->book_file) - 1);

  fclose(f);

  // Try to load learning data
  char learn_file[260];
  snprintf(learn_file, sizeof(learn_file), "%s.learn", filename);
  book_load_learning(book, learn_file);

  return 1;
}

bool book_load_learning(OpeningBook *book, const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (!f)
    return false;

  // Read header
  char magic[4];
  if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "LERN", 4) != 0) {
    fclose(f);
    return false;
  }

  uint32_t version = read_uint32_be(f);
  uint32_t num_entries = read_uint32_be(f);
  (void)version; // Currently unused

  // Read entries
  for (uint32_t i = 0; i < num_entries; i++) {
    uint64_t key = read_uint64_be(f);
    uint8_t move_count;
    if (fread(&move_count, 1, 1, f) != 1)
      break;

    BookEntry *entry = find_or_create_entry(book, key);
    if (!entry)
      continue;

    entry->key = key;
    entry->move_count =
        move_count > MAX_BOOK_MOVES ? MAX_BOOK_MOVES : move_count;

    for (int j = 0; j < entry->move_count; j++) {
      uint32_t move_data = read_uint32_be(f);
      int16_t learn_val, games_val;
      if (fread(&learn_val, 2, 1, f) != 1)
        break;
      if (fread(&games_val, 2, 1, f) != 1)
        break;

      entry->moves[j] = (Move)move_data;
      entry->learn[j] = learn_val;
      entry->games[j] = games_val;
    }
  }

  strncpy(book->learn_file, filename, sizeof(book->learn_file) - 1);
  fclose(f);
  return true;
}

bool book_save_learning(const OpeningBook *book, const char *filename) {
  FILE *f = fopen(filename, "wb");
  if (!f)
    return false;

  // Write header
  fwrite("LERN", 1, 4, f);
  write_uint32_be(f, 1); // Version

  // Count entries with learning data
  uint32_t count = 0;
  for (int i = 0; i < book->capacity; i++) {
    if (book->entries[i].move_count > 0 &&
        (book->entries[i].flags & BOOK_FLAG_MODIFIED)) {
      count++;
    }
  }
  write_uint32_be(f, count);

  // Write entries
  for (int i = 0; i < book->capacity; i++) {
    const BookEntry *entry = &book->entries[i];
    if (entry->move_count == 0 || !(entry->flags & BOOK_FLAG_MODIFIED))
      continue;

    write_uint64_be(f, entry->key);
    fputc(entry->move_count, f);

    for (int j = 0; j < entry->move_count; j++) {
      write_uint32_be(f, (uint32_t)entry->moves[j]);
      fwrite(&entry->learn[j], 2, 1, f);
      fwrite(&entry->games[j], 2, 1, f);
    }
  }

  fclose(f);
  return true;
}

bool book_merge_learning(OpeningBook *book, const char *output_filename) {
  // This would merge learning data back into the Polyglot format
  // For now, just save learning data separately
  (void)book;
  (void)output_filename;
  return false;
}

// ===== BOOK HASH =====

uint64_t book_hash(const Position *pos) { return pos->zobrist; }

// ===== BOOK PROBING =====

Move book_probe(const OpeningBook *book, const Position *pos) {
  if (!book || !book->loaded) {
    return MOVE_NONE;
  }

  ((OpeningBook *)book)->probes++;

  uint64_t key = book_hash(pos);

  // First check enhanced entries for learning data
  BookEntry *enhanced = find_book_entry((OpeningBook *)book, key);

  // Find Polyglot entries
  int poly_idx = find_poly_entry(book, key);

  if (poly_idx < 0 && !enhanced) {
    ((OpeningBook *)book)->misses++;
    return MOVE_NONE;
  }

  ((OpeningBook *)book)->hits++;

  // Collect all moves with their combined scores
  Move moves[MAX_BOOK_MOVES * 2];
  int scores[MAX_BOOK_MOVES * 2];
  int count = 0;
  int total_score = 0;

  // Add Polyglot moves
  if (poly_idx >= 0) {
    while (poly_idx < book->poly_count &&
           book->poly_entries[poly_idx].key == key && count < MAX_BOOK_MOVES) {
      Move m = decode_poly_move(pos, book->poly_entries[poly_idx].move);
      if (m != MOVE_NONE) {
        int weight = book->poly_entries[poly_idx].weight;
        int learn_bonus = 0;

        // Check for learning data
        if (enhanced) {
          for (int j = 0; j < enhanced->move_count; j++) {
            if (enhanced->moves[j] == m) {
              learn_bonus = enhanced->learn[j];
              break;
            }
          }
        }

        int score = weight + learn_bonus;
        if (score < 1)
          score = 1; // Minimum score of 1

        moves[count] = m;
        scores[count] = score;
        total_score += score;
        count++;
      }
      poly_idx++;
    }
  }

  // Add any custom moves from enhanced entries
  if (enhanced) {
    for (int i = 0; i < enhanced->move_count; i++) {
      if (enhanced->flags & BOOK_FLAG_CUSTOM) {
        bool found = false;
        for (int j = 0; j < count; j++) {
          if (moves[j] == enhanced->moves[i]) {
            found = true;
            break;
          }
        }
        if (!found && count < MAX_BOOK_MOVES * 2) {
          int score = enhanced->weights[i] + enhanced->learn[i];
          if (score < 1)
            score = 1;
          moves[count] = enhanced->moves[i];
          scores[count] = score;
          total_score += score;
          count++;
        }
      }
    }
  }

  if (count == 0) {
    return MOVE_NONE;
  }

  // Select a move
  Move selected = moves[0];

  if (book->use_weights && total_score > 0) {
    // Apply random factor
    if (book->random_factor > 0 && book_rand_range(100) < book->random_factor) {
      // Random selection (ignoring weights)
      selected = moves[book_rand_range(count)];
    } else {
      // Weighted selection
      int r = book_rand_range(total_score);
      int sum = 0;
      for (int i = 0; i < count; i++) {
        sum += scores[i];
        if (r < sum) {
          selected = moves[i];
          break;
        }
      }
    }
  } else {
    // Uniform random selection
    selected = moves[book_rand_range(count)];
  }

  return selected;
}

BookEntry *book_get_entry(OpeningBook *book, const Position *pos) {
  if (!book)
    return NULL;
  return find_book_entry(book, book_hash(pos));
}

int book_get_moves(const OpeningBook *book, const Position *pos, Move *moves,
                   int *scores, int max_moves) {
  if (!book || !book->loaded || !moves)
    return 0;

  uint64_t key = book_hash(pos);
  int poly_idx = find_poly_entry(book, key);
  BookEntry *enhanced = find_book_entry((OpeningBook *)book, key);

  int count = 0;

  // Add Polyglot moves
  if (poly_idx >= 0) {
    while (poly_idx < book->poly_count &&
           book->poly_entries[poly_idx].key == key && count < max_moves) {
      Move m = decode_poly_move(pos, book->poly_entries[poly_idx].move);
      if (m != MOVE_NONE) {
        moves[count] = m;
        if (scores) {
          int weight = book->poly_entries[poly_idx].weight;
          int learn_bonus = 0;
          if (enhanced) {
            for (int j = 0; j < enhanced->move_count; j++) {
              if (enhanced->moves[j] == m) {
                learn_bonus = enhanced->learn[j];
                break;
              }
            }
          }
          scores[count] = weight + learn_bonus;
        }
        count++;
      }
      poly_idx++;
    }
  }

  return count;
}

// ===== LEARNING =====

void book_learn(OpeningBook *book, uint64_t key, Move move, BookResult result) {
  if (!book || !book->learning_enabled)
    return;

  BookEntry *entry = find_or_create_entry(book, key);
  if (!entry)
    return;

  // Find the move in the entry
  int move_idx = -1;
  for (int i = 0; i < entry->move_count; i++) {
    if (entry->moves[i] == move) {
      move_idx = i;
      break;
    }
  }

  // Add move if not found
  if (move_idx < 0) {
    if (entry->move_count >= MAX_BOOK_MOVES)
      return;
    move_idx = entry->move_count;
    entry->moves[move_idx] = move;
    entry->weights[move_idx] = 50; // Default weight
    entry->learn[move_idx] = 0;
    entry->games[move_idx] = 0;
    entry->move_count++;
  }

  // Update learning value
  int delta = 0;
  switch (result) {
  case BOOK_RESULT_WIN:
    delta = LEARN_WIN_BONUS;
    break;
  case BOOK_RESULT_LOSS:
    delta = -LEARN_LOSS_PENALTY;
    break;
  case BOOK_RESULT_DRAW:
    delta = LEARN_DRAW_BONUS;
    break;
  }

  entry->learn[move_idx] += delta;

  // Clamp learning value
  if (entry->learn[move_idx] < LEARN_MIN)
    entry->learn[move_idx] = LEARN_MIN;
  if (entry->learn[move_idx] > LEARN_MAX)
    entry->learn[move_idx] = LEARN_MAX;

  // Update games count
  entry->games[move_idx]++;

  // Mark as modified
  entry->flags |= BOOK_FLAG_MODIFIED;

  book->learning_updates++;
}

void book_learn_game(OpeningBook *book, uint64_t *keys, Move *moves,
                     BookResult result, int num_positions) {
  if (!book || !book->learning_enabled || !keys || !moves)
    return;

  // For each position in the game
  for (int i = 0; i < num_positions; i++) {
    // Determine result from the perspective of the side to move
    // Even positions = white to move, odd = black to move (assuming game starts
    // with white)
    BookResult pos_result = result;
    if (i % 2 == 1) {
      // Black's perspective
      if (result == BOOK_RESULT_WIN)
        pos_result = BOOK_RESULT_LOSS;
      else if (result == BOOK_RESULT_LOSS)
        pos_result = BOOK_RESULT_WIN;
    }

    book_learn(book, keys[i], moves[i], pos_result);
  }
}

void book_clear_learning(OpeningBook *book) {
  if (!book || !book->entries)
    return;

  for (int i = 0; i < book->capacity; i++) {
    for (int j = 0; j < book->entries[i].move_count; j++) {
      book->entries[i].learn[j] = 0;
      book->entries[i].games[j] = 0;
    }
    book->entries[i].flags &= ~BOOK_FLAG_MODIFIED;
  }

  book->learning_updates = 0;
}

int book_get_learn_value(const OpeningBook *book, uint64_t key, Move move) {
  if (!book)
    return 0;

  BookEntry *entry = find_book_entry((OpeningBook *)book, key);
  if (!entry)
    return 0;

  for (int i = 0; i < entry->move_count; i++) {
    if (entry->moves[i] == move) {
      return entry->learn[i];
    }
  }

  return 0;
}

// ===== BOOK MANAGEMENT =====

bool book_add_position(OpeningBook *book, uint64_t key, Move move, int weight) {
  if (!book)
    return false;

  BookEntry *entry = find_or_create_entry(book, key);
  if (!entry)
    return false;

  // Check if move already exists
  for (int i = 0; i < entry->move_count; i++) {
    if (entry->moves[i] == move) {
      entry->weights[i] = (int16_t)weight;
      return true;
    }
  }

  // Add new move
  if (entry->move_count >= MAX_BOOK_MOVES)
    return false;

  int idx = entry->move_count;
  entry->moves[idx] = move;
  entry->weights[idx] = (int16_t)weight;
  entry->learn[idx] = 0;
  entry->games[idx] = 0;
  entry->move_count++;
  entry->flags |= BOOK_FLAG_CUSTOM | BOOK_FLAG_MODIFIED;

  return true;
}

bool book_remove_move(OpeningBook *book, uint64_t key, Move move) {
  if (!book)
    return false;

  BookEntry *entry = find_book_entry(book, key);
  if (!entry)
    return false;

  for (int i = 0; i < entry->move_count; i++) {
    if (entry->moves[i] == move) {
      // Shift remaining moves
      for (int j = i; j < entry->move_count - 1; j++) {
        entry->moves[j] = entry->moves[j + 1];
        entry->weights[j] = entry->weights[j + 1];
        entry->learn[j] = entry->learn[j + 1];
        entry->games[j] = entry->games[j + 1];
      }
      entry->move_count--;
      entry->flags |= BOOK_FLAG_MODIFIED;
      return true;
    }
  }

  return false;
}

bool book_set_weight(OpeningBook *book, uint64_t key, Move move, int weight) {
  if (!book)
    return false;

  BookEntry *entry = find_book_entry(book, key);
  if (!entry)
    return false;

  for (int i = 0; i < entry->move_count; i++) {
    if (entry->moves[i] == move) {
      entry->weights[i] = (int16_t)weight;
      entry->flags |= BOOK_FLAG_MODIFIED;
      return true;
    }
  }

  return false;
}

// ===== CONFIGURATION =====

void book_set_learning(OpeningBook *book, bool enabled) {
  if (book)
    book->learning_enabled = enabled;
}

void book_set_random_factor(OpeningBook *book, int factor) {
  if (book) {
    if (factor < 0)
      factor = 0;
    if (factor > 100)
      factor = 100;
    book->random_factor = factor;
  }
}

void book_set_use_weights(OpeningBook *book, bool use_weights) {
  if (book)
    book->use_weights = use_weights;
}

// ===== STATISTICS =====

void book_get_stats(const OpeningBook *book, int *probes, int *hits,
                    int *misses, int *updates) {
  if (!book)
    return;
  if (probes)
    *probes = book->probes;
  if (hits)
    *hits = book->hits;
  if (misses)
    *misses = book->misses;
  if (updates)
    *updates = book->learning_updates;
}

void book_reset_stats(OpeningBook *book) {
  if (!book)
    return;
  book->probes = 0;
  book->hits = 0;
  book->misses = 0;
}

void book_print_info(const OpeningBook *book) {
  if (!book) {
    printf("Book: Not initialized\n");
    return;
  }

  printf("=== Opening Book Info ===\n");
  printf("Loaded: %s\n", book->loaded ? "Yes" : "No");
  printf("Book file: %s\n", book->book_file[0] ? book->book_file : "None");
  printf("Polyglot entries: %d\n", book->poly_count);
  printf("Enhanced entries: %d\n", book->num_entries);
  printf("Learning enabled: %s\n", book->learning_enabled ? "Yes" : "No");
  printf("Use weights: %s\n", book->use_weights ? "Yes" : "No");
  printf("Random factor: %d%%\n", book->random_factor);
  printf("\nStatistics:\n");
  printf("  Probes: %d\n", book->probes);
  printf("  Hits: %d (%.1f%%)\n", book->hits,
         book->probes > 0 ? 100.0 * book->hits / book->probes : 0.0);
  printf("  Misses: %d\n", book->misses);
  printf("  Learning updates: %d\n", book->learning_updates);
}

void book_print_moves(const OpeningBook *book, const Position *pos) {
  if (!book || !pos)
    return;

  Move moves[MAX_BOOK_MOVES * 2];
  int scores[MAX_BOOK_MOVES * 2];
  int count = book_get_moves(book, pos, moves, scores, MAX_BOOK_MOVES * 2);

  if (count == 0) {
    printf("No book moves for this position\n");
    return;
  }

  printf("Book moves (%d):\n", count);

  int total = 0;
  for (int i = 0; i < count; i++) {
    total += scores[i];
  }

  for (int i = 0; i < count; i++) {
    int from = MOVE_FROM(moves[i]);
    int to = MOVE_TO(moves[i]);
    int promo = MOVE_PROMO(moves[i]);

    char move_str[8];
    move_str[0] = 'a' + (from % 8);
    move_str[1] = '1' + (from / 8);
    move_str[2] = 'a' + (to % 8);
    move_str[3] = '1' + (to / 8);
    if (promo) {
      const char promos[] = "nbrq";
      move_str[4] = promos[promo - 1];
      move_str[5] = '\0';
    } else {
      move_str[4] = '\0';
    }

    int learn = book_get_learn_value(book, pos->zobrist, moves[i]);
    double pct = total > 0 ? 100.0 * scores[i] / total : 0.0;

    printf("  %s: score=%d (%.1f%%), learn=%d\n", move_str, scores[i], pct,
           learn);
  }
}
