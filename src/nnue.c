#include "nnue.h"
#include "bitboard.h"
#include "position.h"
#include "types.h"
#include <immintrin.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef EMBED_NNUE
#include "inc_net.h"
#endif

int nnue_available = 0;

// Layer 1: Feature Transformer (40960 -> 256)
static alignas(32) int16_t feature_weights[NNUE_INPUT_DIM * NNUE_HIDDEN_DIM];
static alignas(32) int16_t feature_biases[NNUE_HIDDEN_DIM];

// Layer 2: Output (256x2 -> 1)
static alignas(32) int16_t output_weights[NNUE_HIDDEN_DIM * 2];
static int16_t output_bias;

// Activation function: Clipped ReLU
static inline int16_t clipped_relu(int16_t x) {
  if (x <= 0)
    return 0;
  if (x >= 255)
    return 255;
  return x;
}

// Get the Half-KP index for a piece
// White perspective uses white king, black perspective uses black king
// (mirrored)
static inline int get_halfkp_index(int king_sq, int piece, int color, int sq) {
  if (piece == KING)
    return -1;
  // Map pieces to 0-9: White(P,N,B,R,Q), Black(P,N,B,R,Q)
  int p_idx = piece + (color == WHITE ? 0 : 5);
  return king_sq * 640 + p_idx * 64 + sq;
}

void nnue_init() {
  nnue_available = 0;
  // Initialize with zeros or random if needed, but usually we load a net
  memset(feature_weights, 0, sizeof(feature_weights));
  memset(feature_biases, 0, sizeof(feature_biases));
  memset(output_weights, 0, sizeof(output_weights));
  output_bias = 0;
}

int nnue_load(const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (!f) {
    printf("NNUE: Failed to open network file %s\n", filename);
    return 0;
  }

  // Very basic loading logic (needs to match training output)
  size_t read = 0;
  read += fread(feature_weights, sizeof(int16_t),
                NNUE_INPUT_DIM * NNUE_HIDDEN_DIM, f);
  read += fread(feature_biases, sizeof(int16_t), NNUE_HIDDEN_DIM, f);
  read += fread(output_weights, sizeof(int16_t), NNUE_HIDDEN_DIM * 2, f);
  read += fread(&output_bias, sizeof(int16_t), 1, f);

  fclose(f);
  nnue_available = 1;
  printf("NNUE: Loaded network %s\n", filename);
  return 1;
}

int nnue_load_embedded() {
#ifdef EMBED_NNUE
  const uint8_t *p = nnue_data;

  memcpy(feature_weights, p, sizeof(feature_weights));
  p += sizeof(feature_weights);

  memcpy(feature_biases, p, sizeof(feature_biases));
  p += sizeof(feature_biases);

  memcpy(output_weights, p, sizeof(output_weights));
  p += sizeof(output_weights);

  memcpy(&output_bias, p, sizeof(output_bias));

  nnue_available = 1;
  printf("NNUE: Loaded embedded network\n");
  return 1;
#else
  return 0;
#endif
}

// Full refresh of the accumulator (called on King moves or start of evaluation)
void nnue_refresh_accumulator(const Position *pos) {
  Accumulator *acc = (Accumulator *)&pos->accumulator;

  // Initialize with biases
  for (int i = 0; i < NNUE_HIDDEN_DIM; i++) {
    acc->white[i] = feature_biases[i];
    acc->black[i] = feature_biases[i];
  }

  int w_king_sq = LSB(pos->pieces[WHITE][KING]);
  int b_king_sq = LSB(pos->pieces[BLACK][KING]);
  int b_king_sq_mirrored = b_king_sq ^ 56;

  for (int color = WHITE; color <= BLACK; color++) {
    for (int piece = PAWN; piece < KING; piece++) {
      Bitboard bb = pos->pieces[color][piece];
      while (bb) {
        int sq = pop_lsb(&bb);

        // White Perspective
        int w_idx = get_halfkp_index(w_king_sq, piece, color, sq);

        // Black Perspective (mirrored)
        int b_idx =
            get_halfkp_index(b_king_sq_mirrored, piece, color ^ 1, sq ^ 56);

        // Add features to accumulators
        for (int i = 0; i < NNUE_HIDDEN_DIM; i++) {
          acc->white[i] += feature_weights[w_idx * NNUE_HIDDEN_DIM + i];
          acc->black[i] += feature_weights[b_idx * NNUE_HIDDEN_DIM + i];
        }
      }
    }
  }
}

// Incremental update of the accumulator
void nnue_update_accumulator(Accumulator *acc, int piece, int color, int sq,
                             int is_add, int w_king_sq, int b_king_sq) {
  if (piece == KING)
    return; // Full refresh handled elsewhere

  int b_king_sq_mirrored = b_king_sq ^ 56;
  int w_idx = get_halfkp_index(w_king_sq, piece, color, sq);
  int b_idx = get_halfkp_index(b_king_sq_mirrored, piece, color ^ 1, sq ^ 56);

  if (is_add) {
    for (int i = 0; i < NNUE_HIDDEN_DIM; i++) {
      acc->white[i] += feature_weights[w_idx * NNUE_HIDDEN_DIM + i];
      acc->black[i] += feature_weights[b_idx * NNUE_HIDDEN_DIM + i];
    }
  } else {
    for (int i = 0; i < NNUE_HIDDEN_DIM; i++) {
      acc->white[i] -= feature_weights[w_idx * NNUE_HIDDEN_DIM + i];
      acc->black[i] -= feature_weights[b_idx * NNUE_HIDDEN_DIM + i];
    }
  }
}

// Evaluation function
int nnue_evaluate(const Position *pos) {
  if (!nnue_available)
    return 0;

  const Accumulator *acc = &pos->accumulator;
  int output = output_bias;

  // Layer 2: Clipped ReLU and Linear Output
#ifdef __AVX2__
  __m256i sum_vec = _mm256_setzero_si256();
  __m256i zero = _mm256_setzero_si256();
  __m256i mask255 = _mm256_set1_epi16(255);

  // White perspective
  for (int i = 0; i < NNUE_HIDDEN_DIM; i += 16) {
    __m256i acc_vec = _mm256_load_si256((__m256i *)&acc->white[i]);
    __m256i active = _mm256_min_epi16(_mm256_max_epi16(acc_vec, zero), mask255);
    __m256i weights = _mm256_load_si256((__m256i *)&output_weights[i]);
    sum_vec = _mm256_add_epi32(sum_vec, _mm256_madd_epi16(active, weights));
  }

  // Black perspective
  for (int i = 0; i < NNUE_HIDDEN_DIM; i += 16) {
    __m256i acc_vec = _mm256_load_si256((__m256i *)&acc->black[i]);
    __m256i active = _mm256_min_epi16(_mm256_max_epi16(acc_vec, zero), mask255);
    __m256i weights =
        _mm256_load_si256((__m256i *)&output_weights[NNUE_HIDDEN_DIM + i]);
    sum_vec = _mm256_add_epi32(sum_vec, _mm256_madd_epi16(active, weights));
  }

  // Horizontal sum
  int32_t temp_sums[8];
  _mm256_storeu_si256((__m256i *)temp_sums, sum_vec);
  for (int i = 0; i < 8; i++)
    output += temp_sums[i];

#else
  for (int i = 0; i < NNUE_HIDDEN_DIM; i++) {
    output += clipped_relu(acc->white[i]) * output_weights[i];
    output += clipped_relu(acc->black[i]) * output_weights[NNUE_HIDDEN_DIM + i];
  }
#endif

  // Flip score for black
  return (pos->to_move == WHITE ? output : -output) / NNUE_FACTOR;
}
