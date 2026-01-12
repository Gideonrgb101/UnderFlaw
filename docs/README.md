# C Chess Engine with UCI Protocol Support

A high-performance chess engine written in C featuring advanced search techniques, efficient board representation, and full UCI protocol support.

## Features

### Board Representation

- **Bitboards**: 64-bit integer representation for efficient board operations
- **Magic Bitboards**: O(1) slider piece (rook/bishop) move generation using pre-computed lookup tables
- **Bitboard Utilities**: Compiler intrinsics for bit operations (popcount, LSB, MSB) with fallback implementations

### Search Algorithm

- **Negamax Search**: Recursive search with alpha-beta pruning
- **Principal Variation Search (PVS)**: Enhanced alpha-beta pruning (NegaScout)
- **Iterative Deepening**: Progressive deepening for better move ordering and time management
- **Quiescence Search**: Captures and promotions search to avoid horizon effect
- **Check Extension**: Automatically extends search depth when in check
- **Null Move Pruning**: Forward pruning when not in check
- **Futility Pruning**: Prunes leaf nodes when evaluation is far below alpha
- **Late Move Reductions (LMR)**: Reduces depth for moves searched after alpha-beta fail
- **Move Ordering**: Capture moves prioritized over quiet moves

### Evaluation

- **Material Counting**: Piece value evaluation
- **Piece-Square Tables**: Position-based evaluation with opening/endgame phases
- **Tapered Evaluation**: Smooth transition between opening and endgame evaluation
- **Mobility Evaluation**: Bonus for piece mobility
- **Pawn Structure**:
  - Doubled pawn penalty
  - Isolated pawn penalty
  - Passed pawn bonus with advancement incentive
- **King Safety**: Pawn shelter evaluation

### Transposition Table

- **Zobrist Hashing**: 64-bit position hashing for transposition table lookups
- **Pawn Hash**: Separate hash for pawn structure caching
- **Configurable Size**: TT size adjustable via UCI setoption (1-1024 MB)
- **TT Entry**: Stores score, depth, and bound type (exact, lower, upper)

### Move Generation

- **All Legal Moves**: Complete move generation with legality checking
- **Capture-Only Moves**: For quiescence search
- **Special Moves**:
  - Castling (kingside and queenside for both colors)
  - En passant capture
  - Pawn promotion (to N, B, R, Q)

### Chess Rules Support

- **Castling Rights**: Tracked and validated during move application
- **En Passant**: Correct en passant capture and target square calculation
- **Promotion**: Full promotion to all piece types
- **50-Move Rule**: Halfmove clock for draw detection
- **Checkmate/Stalemate**: Proper detection
- **Check Detection**: Efficient check status evaluation

### UCI Protocol

- **Standard UCI Commands**: uci, isready, position, go, setoption, quit
- **Time Management**:
  - Movetime allocation
  - Incremental time with wtime/btime/winc/binc
  - Adaptive time allocation
- **Debug Output**: Depth, score, nodes, time, NPS reporting
- **Options**: Hash size and search depth configuration

## Build Instructions

### Using CMake

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### Using GNU Make

```bash
make
```

For debug build:

```bash
make debug
```

### Using MSVC (Windows)

```bash
cl /O2 /Oi /arch:AVX2 main.c magic.c position.c movegen.c evaluation.c tt.c search.c uci.c
```

## Usage

The engine communicates via the UCI (Universal Chess Interface) protocol, compatible with chess GUIs like Arena, ChessPAD, and others.

### Starting the Engine

```bash
./chess_engine
```

### UCI Example Session

```
uci
position startpos
go depth 20 movetime 5000
quit
```

### Setting Options

```
setoption name Hash value 256
setoption name Depth value 32
```

## Architecture

### Core Components

1. **bitboard.h**: Bitboard data structure and utility macros
2. **magic.h/magic.c**: Magic bitboard initialization and lookup
3. **types.h**: Move encoding, piece definitions, scoring constants
4. **position.h/position.c**: Board state, move making, zobrist hashing
5. **movegen.h/movegen.c**: Legal move generation
6. **evaluation.h/evaluation.c**: Position evaluation and scoring
7. **tt.h/tt.c**: Transposition table implementation
8. **search.h/search.c**: Search algorithms and iterative deepening
9. **uci.h/uci.c**: UCI protocol implementation
10. **main.c**: Entry point and initialization

### Move Representation

```c
typedef uint32_t Move;

// Encoding:
// Bits 0-5:   Source square (0-63)
// Bits 6-11:  Destination square (0-63)
// Bits 12-15: Promotion piece (0 = none, 1-4 = N,B,R,Q)
// Bits 14-15: Move flag (0 = quiet, 1 = capture, 2 = special)
```

### Transposition Table Entry

```c
typedef struct {
    uint64_t key;      // Zobrist hash key
    Score score;       // Evaluation score
    int16_t depth;     // Search depth
    uint8_t flag;      // Bound type (exact/lower/upper)
} TTEntry;
```

## Performance Optimizations

1. **Compiler Intrinsics**: Uses \__builtin_\* functions for GCC/Clang and intrinsics for MSVC
2. **LTO**: Link-time optimization enabled in Release builds
3. **SIMD**: Architecture-specific optimization flags (-march=native, /arch:AVX2)
4. **Memory Efficient**: Compact move representation and entry structures
5. **Inline Functions**: Hot path functions marked for inlining
6. **Bitboard Iteration**: Efficient LSB scanning for board traversal

## Strength and Playing Level

Expected ELO (approximate):

- At 1 second/move: ~1400-1600
- At 5 seconds/move: ~1700-1900
- At 10+ seconds/move: ~2000-2200

The engine can be strengthened by:

- Increasing hash table size
- Deepening search more aggressively
- Improving evaluation function with more sophisticated bonuses/penalties
- Adding killer move heuristics
- Implementing history heuristics for move ordering

## Future Enhancement Ideas

- [ ] Killer move heuristics
- [ ] History heuristics for move ordering
- [ ] Opening book support
- [ ] Tablebases for endgame (Syzygy or WDL)
- [ ] Multi-threading support with parallel search
- [ ] Better evaluation function with neural networks
- [ ] Aspiration windows for faster alpha-beta cutoffs
- [ ] Transposition table aging
- [ ] More sophisticated time allocation

## Debugging

The engine includes a debug command 'd' to print the current position in FEN format:

```
d
```

To enable verbose output, compile with debug flag and send:

```
setoption name Debug value 1
```

## License

This implementation is provided as an educational example of modern chess engine techniques.

## References

- Chess Programming Wiki: https://www.chessprogramming.org/
- UCI Protocol Specification
- Move Encoding and Magic Bitboards
- Alpha-Beta Pruning and Principal Variation Search
- Transposition Tables and Zobrist Hashing
