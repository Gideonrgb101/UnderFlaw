#include "perft.h"
#include "movegen.h"
#include "position.h"
#include "threads.h"
#include "types.h"
#include "uci.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
    Perft (Performance Test) is a debugging function that walks the move
   generation tree and counts all the leaf nodes at a specific depth. This is
   crucial for validting the move generator and board state updates.
*/

void perft_init() {
  // Nothing to init for now
}

uint64_t perft(Position *pos, int depth) {
  if (depth == 0) {
    return 1;
  }

  MoveList moves;
  movegen_all(pos, &moves);

  uint64_t nodes = 0;

  for (int i = 0; i < moves.count; i++) {
    Move move = moves.moves[i];

    // We need to check legality before counting or recursing
    // movegen_all generates pseudo-legal moves for sliding pieces,
    // but fully verifies checks for castling.
    // However, standard perft implementations often filter legality inside the
    // loop. Our movegen_is_legal() checks if the move leaves the king in check.

    if (!movegen_is_legal(pos, move))
      continue;

    if (depth == 1) {
      nodes++;
    } else {
      Position next_pos = *pos;
      UndoInfo undo;
      position_make_move(&next_pos, move, &undo);
      nodes += perft(&next_pos, depth - 1);
    }
  }

  return nodes;
}

void perft_divide(Position *pos, int depth) {
  printf("Perft Divide Depth %d\n", depth);
  printf("================================\n");

  uint64_t total_nodes = 0;
  int start = get_time_ms();

  MoveList moves;
  movegen_all(pos, &moves);

  for (int i = 0; i < moves.count; i++) {
    Move move = moves.moves[i];

    if (!movegen_is_legal(pos, move))
      continue;

    Position next_pos = *pos;
    UndoInfo undo;
    position_make_move(&next_pos, move, &undo);

    uint64_t nodes = perft(&next_pos, depth - 1);

    char move_str[10];
    move_to_string(move, move_str);

    printf("%s: %llu\n", move_str, nodes);
    total_nodes += nodes;
  }

  int end = get_time_ms();
  double time_taken = (double)(end - start) / 1000.0;

  printf("================================\n");
  printf("Total Nodes: %llu\n", total_nodes);
  printf("Time: %.3f s\n", time_taken);
  if (time_taken > 0) {
    printf("NPS: %.0f\n", total_nodes / time_taken);
  }
}

void run_test_suite(const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    printf("Error: Could not open test suite file %s\n", filename);
    return;
  }

  char line[1024];
  int tests_passed = 0;
  int total_tests = 0;

  printf("Running Test Suite: %s\n", filename);

  while (fgets(line, sizeof(line), file)) {
    // Simple EPD parsing: "fen ... ;D1 <nodes> ;D2 <nodes> ..."
    // Example: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 ;D1 20
    // ;D2 400

    // Trim newline
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';

    if (len == 0 || line[0] == '#')
      continue;

    // Split FEN and Data
    char *semicolon = strchr(line, ';');
    if (!semicolon)
      continue;

    *semicolon = '\0';
    char *fen = line;
    char *data = semicolon + 1;

    // Setup position
    Position pos;
    position_from_fen(&pos, fen);

    // Parse depth/node pairs
    char *token = strtok(data, " ;");
    while (token) {
      if (token[0] == 'D' && strlen(token) > 1) {
        int depth = atoi(token + 1);
        token = strtok(NULL, " ;");
        if (token) {
          uint64_t expected_nodes = strtoull(token, NULL, 10);

          uint64_t actual_nodes = perft(&pos, depth);

          total_tests++;
          if (actual_nodes == expected_nodes) {
            tests_passed++;
            // printf("PASS: Depth %d, Nodes %llu\n", depth, actual_nodes);
          } else {
            printf("FAIL: FEN %s\n", fen);
            printf("      Depth %d: Expected %llu, Got %llu\n", depth,
                   expected_nodes, actual_nodes);
          }
        }
      }
      token = strtok(NULL, " ;");
    }
  }

  fclose(file);

  printf("\nTest Suite Completed.\n");
  printf("Passed: %d / %d\n", tests_passed, total_tests);

  if (tests_passed == total_tests)
    printf("RESULT: PASS\n");
  else
    printf("RESULT: FAIL\n");
}

void run_bench(int depth) {
  Position pos;
  // Startpos
  position_from_fen(&pos,
                    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

  printf("Running Bench at Depth %d...\n", depth);
  perft_divide(&pos, depth);
}
