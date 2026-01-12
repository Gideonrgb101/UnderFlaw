#ifndef BOOK_H
#define BOOK_H

#include "position.h"
#include "types.h"
#include <stdint.h>
#include <stdbool.h>

// ===== CONSTANTS =====

#define MAX_BOOK_MOVES 8      // Maximum moves per position
#define LEARN_WIN_BONUS 10    // Learning bonus for wins
#define LEARN_LOSS_PENALTY 10 // Learning penalty for losses
#define LEARN_DRAW_BONUS 2    // Small bonus for draws (optional)
#define LEARN_MIN -100        // Minimum learning value
#define LEARN_MAX 100         // Maximum learning value

// ===== GAME RESULT FOR LEARNING =====

typedef enum
{
    BOOK_RESULT_LOSS = 0,
    BOOK_RESULT_DRAW = 1,
    BOOK_RESULT_WIN = 2
} BookResult;

// ===== POLYGLOT BOOK ENTRY (for loading .bin files) =====

typedef struct
{
    uint64_t key;    // Polyglot hash key
    uint16_t move;   // Encoded move
    uint16_t weight; // Frequency weight
    uint32_t learn;  // Learning data (unused in standard Polyglot)
} PolyglotEntry;

// ===== ENHANCED BOOK ENTRY =====

typedef struct
{
    uint64_t key;                    // Position hash key
    Move moves[MAX_BOOK_MOVES];      // Moves for this position
    int16_t weights[MAX_BOOK_MOVES]; // Base weights (from book file)
    int16_t learn[MAX_BOOK_MOVES];   // Learning adjustments
    int16_t games[MAX_BOOK_MOVES];   // Games played with this move
    uint8_t move_count;              // Number of moves
    uint8_t flags;                   // Flags (e.g., has been updated)
} BookEntry;

// Book entry flags
#define BOOK_FLAG_MODIFIED 0x01
#define BOOK_FLAG_CUSTOM 0x02

// ===== OPENING BOOK STRUCTURE =====

typedef struct
{
    // Original Polyglot entries (for compatibility)
    PolyglotEntry *poly_entries;
    int poly_count;

    // Enhanced book entries (for learning)
    BookEntry *entries;
    int num_entries;
    int capacity;

    // Book settings
    bool loaded;
    bool learning_enabled;
    bool use_weights;  // Use weight-based selection vs uniform
    int random_factor; // 0-100: percentage of randomness in selection

    // Statistics
    int probes;
    int hits;
    int misses;
    int learning_updates;

    // File paths
    char book_file[256];
    char learn_file[256];
} OpeningBook;

// ===== BOOK CREATION/DESTRUCTION =====

// Initialize opening book
OpeningBook *book_create(void);

// Cleanup opening book
void book_delete(OpeningBook *book);

// ===== FILE I/O =====

// Load a Polyglot .bin file
int book_load(OpeningBook *book, const char *filename);

// Load learning data from file
bool book_load_learning(OpeningBook *book, const char *filename);

// Save learning data to file
bool book_save_learning(const OpeningBook *book, const char *filename);

// Merge learning data into book file (creates new .bin)
bool book_merge_learning(OpeningBook *book, const char *output_filename);

// ===== BOOK PROBING =====

// Get a book move for the position (returns MOVE_NONE if not found)
Move book_probe(const OpeningBook *book, const Position *pos);

// Get book entry for position (NULL if not found)
BookEntry *book_get_entry(OpeningBook *book, const Position *pos);

// Get all book moves for a position with their scores
int book_get_moves(const OpeningBook *book, const Position *pos,
                   Move *moves, int *scores, int max_moves);

// Calculate Polyglot hash for a position
uint64_t book_hash(const Position *pos);

// ===== LEARNING =====

// Update book based on game result
// key: position hash, move: move played, result: game outcome
void book_learn(OpeningBook *book, uint64_t key, Move move, BookResult result);

// Update multiple positions from a game
// keys/moves: arrays of positions and moves played
// result: final game result (from white's perspective)
// num_positions: number of positions in the game
void book_learn_game(OpeningBook *book, uint64_t *keys, Move *moves,
                     BookResult result, int num_positions);

// Clear all learning data
void book_clear_learning(OpeningBook *book);

// Get learning value for a move
int book_get_learn_value(const OpeningBook *book, uint64_t key, Move move);

// ===== BOOK MANAGEMENT =====

// Add a new position to the book
bool book_add_position(OpeningBook *book, uint64_t key, Move move, int weight);

// Remove a move from the book
bool book_remove_move(OpeningBook *book, uint64_t key, Move move);

// Set move weight manually
bool book_set_weight(OpeningBook *book, uint64_t key, Move move, int weight);

// ===== CONFIGURATION =====

// Enable/disable learning
void book_set_learning(OpeningBook *book, bool enabled);

// Set random factor (0-100)
void book_set_random_factor(OpeningBook *book, int factor);

// Set whether to use weights
void book_set_use_weights(OpeningBook *book, bool use_weights);

// ===== STATISTICS =====

// Get book statistics
void book_get_stats(const OpeningBook *book, int *probes, int *hits,
                    int *misses, int *updates);

// Reset statistics
void book_reset_stats(OpeningBook *book);

// Print book info (for debugging)
void book_print_info(const OpeningBook *book);

// Print moves for a position (for debugging)
void book_print_moves(const OpeningBook *book, const Position *pos);

#endif // BOOK_H
