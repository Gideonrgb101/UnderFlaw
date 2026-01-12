# Development Guide

This guide is for developers contributing to the C Chess Engine project.

## Getting Started

### Prerequisites

- **Windows:** Visual Studio 2015+, GCC/Clang via MinGW, or MSVC
- **Linux:** GCC 5.0+, Make, development headers
- **macOS:** Xcode Command Line Tools, GCC/Clang

### Initial Setup

1. **Clone the repository**

   ```bash
   git clone <repo-url>
   cd "C engine"
   ```

2. **Install dependencies**

   **Windows (skip if using MSVC):**

   ```powershell
   # Using Chocolatey
   choco install mingw
   ```

   **Linux (Ubuntu/Debian):**

   ```bash
   sudo apt-get install build-essential git cmake
   ```

   **macOS:**

   ```bash
   xcode-select --install
   ```

3. **Build the project**

   ```bash
   cd build
   # Windows
   .\build.bat
   # Linux/macOS
   ./build.sh
   ```

4. **Verify the build**
   ```bash
   # Windows
   dir chess_engine.exe
   # Linux/macOS
   ls -lh chess_engine
   ```

## Project Architecture

### Module Overview

```
search.c/h          - Core alpha-beta search algorithm
  ├─ iterative_deepening()     - Main search entry point
  ├─ alpha_beta()              - Recursive search with pruning
  ├─ quiescence()              - Tactical move evaluation
  ├─ get_aspiration_window()   - Dynamic window sizing
  ├─ update_search_stability() - Volatility tracking
  └─ order_moves()             - Move sorting

movepicker.c/h      - Move generation and ordering
  ├─ generate_moves()   - Legal move generation
  ├─ score_move()       - Move evaluation for ordering
  ├─ see()              - Static exchange evaluation
  └─ see_full()         - Iterative SEE

evaluation.c/h      - Position evaluation
  ├─ evaluate()         - Overall position score
  ├─ material_count()   - Material balance
  ├─ pst_value()        - Piece-square tables
  └─ pawn_structure()   - Pawn evaluation

tt.c/h              - Transposition table
  ├─ tt_store()        - Store position
  ├─ tt_lookup()       - Retrieve position
  └─ tt_clear()        - Clear hash table

position.c/h        - Board representation
  ├─ position_make_move()    - Apply move
  ├─ position_unmake_move()  - Undo move
  ├─ position_legal()        - Check legality
  └─ position_hash()         - Zobrist hashing

uci.c/h             - UCI protocol
  ├─ uci_loop()        - Main UCI command loop
  ├─ uci_position()    - Parse position command
  ├─ uci_go()          - Parse go command
  └─ uci_option()      - Parse setoption command
```

## Code Organization Standards

### File Structure

Each `.c` file should follow this structure:

```c
// Standard includes first
#include <stdio.h>
#include <string.h>

// Project includes
#include "types.h"
#include "position.h"

// Internal function declarations (static)
static int helper_function(int arg);

// Public function implementations
int public_function(int x) {
    // implementation
}

// Internal function implementations
static int helper_function(int arg) {
    // implementation
}
```

### Header Files

Each `.h` file should have:

```c
#ifndef MODULE_H
#define MODULE_H

#include "types.h"  // Required dependencies

// Public type definitions
typedef struct {
    int field1;
    int field2;
} ModuleType;

// Public function declarations
int public_function(int arg);
void another_function(ModuleType *ptr);

#endif // MODULE_H
```

### Naming Conventions

| Category  | Convention                  | Example                               |
| --------- | --------------------------- | ------------------------------------- |
| Constants | `UPPER_SNAKE_CASE`          | `HISTORY_MAX`, `PIECE_COUNT`          |
| Variables | `lower_snake_case`          | `best_move`, `search_depth`           |
| Functions | `lower_snake_case`          | `evaluate_position()`                 |
| Macros    | `UPPER_SNAKE_CASE`          | `LSB(x)`, `COLOR_OF(piece)`           |
| Types     | `lower_snake_case`          | `struct position`, `typedef MoveList` |
| Structs   | `CamelCase` or `snake_case` | `SearchState`, `position_s`           |

### Indentation and Formatting

- **Indentation:** 4 spaces (not tabs)
- **Line length:** Max 100-120 characters
- **Brace style:** K&R (opening brace on same line)

```c
int function(int x) {
    if (x > 0) {
        return x * 2;
    } else {
        return 0;
    }
}
```

## Common Development Tasks

### Adding a New Feature

1. **Plan the feature**

   - Identify which modules need changes
   - Design the data structures
   - Write pseudocode comments

2. **Implement in headers first**

   - Add type definitions
   - Declare functions
   - Document with comments

3. **Implement in source files**

   - Add function implementations
   - Include necessary headers
   - Test individual functions

4. **Integrate with existing code**

   - Update calling functions
   - Ensure proper error handling
   - Test with full engine

5. **Document the feature**
   - Add code comments
   - Update relevant docs in `docs/`
   - Update this guide if broadly applicable

### Fixing a Bug

1. **Reproduce the issue**

   - Create minimal test case
   - Document exact conditions
   - Save position (FEN) if position-specific

2. **Isolate the problem**

   - Add debug output
   - Use GDB or debugger
   - Trace through code path

3. **Implement the fix**

   - Make minimal targeted change
   - Verify it solves the issue
   - Check for side effects

4. **Test thoroughly**

   - Run existing test suite
   - Play matches to verify
   - Test edge cases

5. **Document the fix**
   - Comment explaining the issue
   - Reference any related issues
   - Update CHANGELOG if applicable

### Optimizing Performance

1. **Profile the code**

   ```bash
   # Linux: Use perf
   perf record -g ./chess_engine
   perf report

   # macOS: Use Instruments
   instruments -t "System Trace" ./chess_engine
   ```

2. **Identify bottlenecks**

   - Look for functions in hot path
   - Check allocation/deallocation frequency
   - Measure cache misses

3. **Optimize carefully**

   - Make small changes
   - Measure improvement
   - Document why optimization works

4. **Verify correctness**
   - Check output still correct
   - Run test suite
   - Play matches for strength verification

## Building and Testing

### Build Variants

```bash
# Development build (debug symbols, -O2)
make debug

# Release build (maximum optimization)
make release

# Clean build
make clean && make

# Parallel build (4 cores)
make -j4
```

### Testing

#### Manual Testing

```bash
# Start the engine
./chess_engine

# Commands to test:
# uci
# isready
# position startpos moves e2e4 e7e5
# go depth 10
# quit
```

#### Automated Testing

```bash
# Test EPD positions
./chess_engine < tests/test_positions.epd
```

#### Performance Testing

```bash
# Measure nodes per second
./chess_engine

# position startpos
# go depth 15
# [Check output for nodes/second]
```

### Debugging

#### Using GDB on Linux/macOS

```bash
# Compile with debug symbols
make debug

# Run under GDB
gdb ./chess_engine

# GDB commands:
# break search.c:100     - Set breakpoint
# run                    - Start program
# next                   - Step over
# step                   - Step into
# print variable_name    - Print value
# continue               - Continue execution
# bt                     - Print backtrace
# info locals            - Local variables
```

#### Using MSVC Debugger (Windows)

```bash
# Build in Debug mode
cl /Zi /Od src/*.c /Fe:chess_engine_debug.exe

# Then debug in Visual Studio or VS Code
```

### Performance Profiling

#### Linux (using perf)

```bash
# Record profile data
perf record -g -p $(pgrep chess_engine)

# View results
perf report

# Flamegraph
perf script > out.perf
# Use FlameGraph tools to visualize
```

#### macOS (using Instruments)

```bash
instruments -t "System Trace" ./chess_engine
# Opens Xcode Instruments GUI
```

#### All Platforms (simple timing)

```bash
# Time a single search
time ./chess_engine << EOF
position startpos
go depth 15
quit
EOF
```

## Code Review Checklist

Before submitting changes:

- [ ] Code compiles without warnings
- [ ] Follows naming conventions
- [ ] Proper indentation (4 spaces)
- [ ] Comments explain complex logic
- [ ] No unused variables
- [ ] Memory properly allocated/freed
- [ ] Handles error conditions
- [ ] No race conditions
- [ ] Performance impact analyzed
- [ ] Tests pass (if applicable)
- [ ] Documentation updated

## Commit Guidelines

Write clear commit messages:

```
Summary (50 chars max)

Detailed explanation (70 chars max per line)
- Bullet point for change 1
- Bullet point for change 2

Related issue: #123
```

Examples:

```
Add phase-aware futility margins

Adjust futility pruning margins based on game phase:
- +20% margin in endgame for more conservative pruning
- -20% margin in opening for more aggressive pruning
- This improves move accuracy without significant slowdown

Fixes #45
```

## Common Pitfalls

### Memory Management

❌ **Wrong:**

```c
int *arr = malloc(100 * sizeof(int));
// ... use arr ...
// forgot to free!
```

✅ **Right:**

```c
int *arr = malloc(100 * sizeof(int));
// ... use arr ...
free(arr);
arr = NULL;
```

### Off-by-One Errors

❌ **Wrong:**

```c
for (int i = 0; i <= 64; i++) {  // 65 iterations!
    squares[i] = value;
}
```

✅ **Right:**

```c
for (int i = 0; i < 64; i++) {   // 64 iterations
    squares[i] = value;
}
```

### Integer Overflow

❌ **Wrong:**

```c
int score = pieces[i].value * position.phase;  // Can overflow
```

✅ **Right:**

```c
long score = (long)pieces[i].value * position.phase;
```

### Uninitialized Variables

❌ **Wrong:**

```c
int best_move;
if (something) {
    best_move = 42;
}
return best_move;  // Uninitialized if something false
```

✅ **Right:**

```c
int best_move = 0;
if (something) {
    best_move = 42;
}
return best_move;
```

## Performance Tips

### Cache Efficiency

- Keep hot data near each other
- Use consistent access patterns
- Avoid pointer chasing in tight loops

### Optimization Priorities

1. Algorithmic efficiency (choose better algorithm)
2. Memory efficiency (reduce allocations)
3. CPU efficiency (inline, reduce branches)
4. Micro-optimizations (last resort)

### Profiling First

Always profile before optimizing:

- Don't guess about bottlenecks
- Measure actual improvement
- Verify correctness still holds

## Documentation Guidelines

### Code Comments

```c
// Good: explains WHY
// Use negative null move depth to distinguish from normal moves
int null_move_depth = -(depth - reduction);

// Bad: explains WHAT (code already shows this)
// Set null_move_depth to negative
int null_move_depth = -(depth - reduction);
```

### Function Documentation

```c
/**
 * Evaluate the current position.
 *
 * Args:
 *   pos: Position to evaluate
 *   ply: Current search depth
 *   material_only: If true, only count material
 *
 * Returns:
 *   Score in centipawns (positive = white better)
 *
 * Notes:
 *   This function should be called after legal moves are verified.
 *   Caches results in evaluation cache.
 */
int evaluate(Position *pos, int ply, int material_only)
```

### File Headers

```c
/**
 * search.c - Alpha-beta search with transposition tables
 *
 * Implements:
 * - Iterative deepening
 * - Alpha-beta pruning with aspiration windows
 * - Transposition table lookup/storage
 * - Quiescence search for tactical evaluation
 *
 * Main functions:
 * - iterative_deepening(): Entry point for search
 * - alpha_beta(): Recursive search function
 * - quiescence(): Tactical move evaluation
 */
```

## Contributing to Documentation

### Document Locations

- **Implementation details:** `docs/IMPLEMENTATION_REPORT.md`
- **Feature descriptions:** `docs/IMPROVEMENTS.md`
- **Parameter tuning:** `assets/PARAMETERS.md`
- **Architecture overview:** `docs/QUICK_REFERENCE.md`
- **Build instructions:** `BUILDING.md`

### Updating Documentation

1. Keep documentation in sync with code
2. Use clear, concise language
3. Include examples where helpful
4. Link to related sections
5. Update table of contents

## Release Process

1. **Version numbering:** MAJOR.MINOR (e.g., 2.0)
2. **Update version** in relevant files
3. **Update CHANGELOG** with changes
4. **Run full test suite**
5. **Build release binaries**
6. **Create release notes**
7. **Tag in git:** `git tag v2.0`

## Continuous Integration

The project uses automated testing:

- Builds on every commit
- Runs test suite
- Checks for compiler warnings
- Generates performance reports

View status in `.github/workflows/` (if configured).

## Getting Help

1. **Code questions:** Check `docs/QUICK_REFERENCE.md`
2. **Build issues:** See `BUILDING.md`
3. **Configuration:** See `assets/PARAMETERS.md`
4. **Algorithm details:** See `docs/FINAL_REPORT.md`
5. **Module details:** See `PROJECT_STRUCTURE.md`

---

**Last Updated:** 2024  
**Engine Version:** 2.0
