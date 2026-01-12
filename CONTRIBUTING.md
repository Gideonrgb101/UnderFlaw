# Contributing to C Chess Engine

Thank you for your interest in contributing to the C Chess Engine project! This document provides guidelines and instructions for contributing.

## Code of Conduct

- Be respectful and constructive in all interactions
- Welcome diverse perspectives and experience levels
- Focus on the code, not the person
- Help others succeed in contributing

## Getting Started

### Prerequisites

- C compiler (GCC, Clang, or MSVC)
- Git
- Basic understanding of chess and/or C programming
- Familiarity with the project structure (see `PROJECT_STRUCTURE.md`)

### Setup

1. **Fork the repository** (if applicable)
2. **Clone your fork**

   ```bash
   git clone <your-fork-url>
   cd "C engine"
   ```

3. **Create a feature branch**

   ```bash
   git checkout -b feature/your-feature-name
   ```

4. **Build and test**
   ```bash
   cd build
   ./build.sh  # or build.bat on Windows
   ```

## Types of Contributions

### Bug Reports

Found a bug? Please report it:

1. **Check existing issues** - Avoid duplicates
2. **Create a clear title** - Describe the problem briefly
3. **Provide details:**
   - Operating system and compiler
   - Exact commands to reproduce
   - Expected vs actual behavior
   - FEN position (if position-specific)
4. **Include engine output:**
   ```
   go depth 15
   [paste resulting output]
   ```

### Feature Requests

Have an idea? We'd love to hear it:

1. **Check the roadmap** - Might already be planned
2. **Describe the feature** - Why would it be useful?
3. **Provide context** - Link to related features or papers
4. **Estimate impact** - Expected ELO gain or performance improvement

### Code Contributions

#### Improvements to Existing Code

1. Optimize performance
2. Fix inefficiencies
3. Improve readability
4. Add missing comments

#### New Features

1. Internal Iterative Deepening improvements
2. Move ordering enhancements
3. Evaluation refinements
4. Parameter tuning optimizations

#### Documentation

1. Clarify existing docs
2. Add missing documentation
3. Fix typos
4. Add examples

## Development Workflow

### Step 1: Create Your Feature Branch

```bash
git checkout -b feature/add-feature-name
```

Branch naming conventions:

- `feature/` - New features
- `bugfix/` - Bug fixes
- `refactor/` - Code refactoring
- `docs/` - Documentation updates
- `perf/` - Performance improvements

### Step 2: Make Your Changes

#### Guidelines

- **One feature per branch** - Keep commits focused
- **Small, logical commits** - Easy to review and revert
- **Write descriptive messages** - Future you will thank you

#### Example Commit

```bash
git add src/search.c include/search.h

git commit -m "Add stability-aware LMR adjustment

- Implement stability_aware_lmr() function
- Track position volatility across iterations
- Reduce aggressively in stable positions
- Increase conservatively in volatile positions

This change improves move quality in complex positions
while maintaining speed in simplified positions.

Estimated impact: +3-5 ELO"
```

### Step 3: Test Your Changes

#### Run the Build

```bash
cd build
make clean
make
```

Check for:

- No compiler warnings
- Executable size reasonable (~200-300 KB)
- No undefined references

#### Test Basic Functionality

```bash
./chess_engine << EOF
uci
isready
position startpos
go depth 10
quit
EOF
```

#### Test with Positions

```bash
position fen "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 1"
go depth 12
```

#### Run Test Suite

```bash
./chess_engine < tests/test_positions.epd
```

#### Verify No Regressions

- Play matches against previous version
- Track ELO ratings
- Test on multiple position types

### Step 4: Write Tests (If Applicable)

For significant features, add test cases:

```c
// In tests/test_new_feature.c
void test_feature_basic() {
    // Arrange
    Position pos = setup_test_position();

    // Act
    int result = new_feature(&pos);

    // Assert
    assert(result == expected_value);
}
```

### Step 5: Update Documentation

1. **Update relevant docs:**

   - `docs/FINAL_REPORT.md` - Feature overview
   - `docs/QUICK_REFERENCE.md` - Function signatures
   - `DEVELOPMENT.md` - If workflow affected
   - `CHANGELOG.md` - Under "Unreleased" section

2. **Add code comments:**

   ```c
   /**
    * Evaluate move quality using new heuristic.
    *
    * This function implements the new feature that...
    *
    * Args:
    *   pos: Position to evaluate
    *   move: Move to score
    *
    * Returns:
    *   Score in centipawns
    */
   int score_move_new_heuristic(Position *pos, Move move)
   ```

3. **Update headers:**
   - Add function declarations to `.h` files
   - Export public functions only

### Step 6: Create Pull Request

1. **Push to your fork**

   ```bash
   git push origin feature/your-feature-name
   ```

2. **Create pull request on GitHub**

   - Write clear title
   - Reference any related issues
   - Describe changes and rationale
   - Include performance impact (if applicable)

3. **PR Template:**

   ```markdown
   ## Description

   Brief description of changes

   ## Type of Change

   - [ ] Bug fix
   - [ ] New feature
   - [ ] Performance improvement
   - [ ] Documentation update

   ## Testing

   - [ ] Builds without warnings
   - [ ] Test suite passes
   - [ ] Manual testing done
   - [ ] Matches confirm strength

   ## Performance Impact

   - Estimated ELO: +X
   - Speed change: Y%
   - Memory impact: Z MB

   ## Checklist

   - [ ] Code follows style guide
   - [ ] Documentation updated
   - [ ] No new compiler warnings
   - [ ] Tested on multiple platforms
   ```

## Code Style Guide

### Naming

```c
// Constants (UPPER_SNAKE_CASE)
#define HISTORY_MAX 500
const int PIECE_VALUE[7] = {...};

// Variables (lower_snake_case)
int best_move;
struct position current_pos;

// Functions (lower_snake_case)
int evaluate_position(Position *pos);
void alpha_beta_search(SearchState *state);

// Types
typedef struct position Position;
struct search_state { ... };
```

### Formatting

```c
// Indentation: 4 spaces
if (condition) {
    // Code here
}

// Max line length: 100-120 chars
int very_long_function_name(int param1, int param2,
                            int param3, int param4);

// Space around operators
int result = (a + b) * (c - d);

// No space inside parentheses
func(arg1, arg2);  // Good
func( arg1, arg2 ); // Bad
```

### Comments

```c
// Good: explains WHY
// Alpha-beta with enhanced killer heuristics
// for faster move ordering

// Bad: explains WHAT (code shows this)
// Set move to killer_moves[ply][0]
move = killer_moves[ply][0];
```

## Performance Guidelines

### Before Optimizing

1. **Measure first** - Use perf/profiler
2. **Document baseline** - Record current speed
3. **Identify hotspot** - Which function uses most time?

### During Optimization

1. **Make small changes** - Single concept per change
2. **Measure each change** - Verify improvement
3. **Avoid premature optimization** - Clarity first

### After Optimization

1. **Verify correctness** - Engine still plays well
2. **Document why** - Explain the optimization
3. **Benchmark impact** - Measure speed improvement

## Testing Requirements

### For All Changes

- [ ] Compiles without warnings
- [ ] No undefined references
- [ ] Basic UCI commands work
- [ ] Produces legal moves

### For Features

- [ ] Targeted test cases pass
- [ ] No performance regression
- [ ] Documentation updated
- [ ] Code comments clear

### For Performance Changes

- [ ] Speed improvement measured
- [ ] Correctness verified
- [ ] No strength regression
- [ ] Impact benchmarked

### For Bug Fixes

- [ ] Reproduces the bug
- [ ] Fix eliminates bug
- [ ] No new regressions
- [ ] Root cause documented

## Review Process

### What Reviewers Look For

1. **Correctness** - Does the code work?
2. **Performance** - Is it efficient?
3. **Style** - Does it match guidelines?
4. **Testing** - Is it well tested?
5. **Documentation** - Is it clear?

### Responding to Feedback

- Be open to criticism
- Ask for clarification if needed
- Implement suggested changes
- Re-request review after updates

### Merging

Once approved:

1. Reviewer merges pull request
2. Branch is deleted
3. Change is in main codebase
4. Celebrate! 🎉

## Useful Resources

### Documentation

- `PROJECT_STRUCTURE.md` - Project organization
- `DEVELOPMENT.md` - Development workflow
- `BUILDING.md` - Build instructions
- `docs/FINAL_REPORT.md` - Technical details
- `docs/QUICK_REFERENCE.md` - Function reference

### Tools

- **Editor:** VS Code with C extension
- **Debugger:** GDB (Linux/macOS) or MSVC (Windows)
- **Profiler:** perf (Linux), Instruments (macOS), VTune (Windows)
- **Version Control:** Git

### Learning Resources

- Chess Engine in C by [author]
- Stockfish source code (for reference)
- CPW (Chess Programming Wiki)
- [Specific algorithm papers]

## Common Contributions

### Easy Starter Issues

- [ ] Add comments to undocumented functions
- [ ] Fix typos in documentation
- [ ] Add test positions to EPD suite
- [ ] Improve error messages

### Intermediate Issues

- [ ] Optimize specific function
- [ ] Add new move ordering heuristic
- [ ] Improve position evaluation
- [ ] Enhance parameter tuning

### Advanced Issues

- [ ] Implement new search technique
- [ ] Add threading support
- [ ] Neural network evaluation
- [ ] Tablebase integration

## Community

### Getting Help

1. **GitHub Issues** - Ask questions
2. **Discussions** - General chat
3. **Mailing List** - Long-form discussion
4. **Discord** - Real-time chat (if applicable)

### Recognition

Contributors are recognized in:

- `AUTHORS.md` file
- Release notes
- Project README
- GitHub contributor graph

## License

By contributing, you agree that your contributions will be licensed under the same license as the project.

---

## Summary

1. **Fork and clone** the repository
2. **Create a feature branch** with clear naming
3. **Make focused changes** with clear commits
4. **Test thoroughly** with multiple strategies
5. **Update documentation** and add comments
6. **Create pull request** with clear description
7. **Respond to feedback** constructively
8. **Celebrate** your contribution! 🎉

Thank you for contributing to making the C Chess Engine better!

---

**Last Updated:** 2024  
**Version:** 2.0
