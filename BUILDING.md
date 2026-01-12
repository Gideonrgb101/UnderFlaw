# Building the Chess Engine

This guide covers all methods to build the C Chess Engine.

## Quick Start

### Windows

```bash
cd build
build.bat
```

### Linux/macOS

```bash
cd build
./build.sh
```

### CMake (All Platforms)

```bash
mkdir build_cmake
cd build_cmake
cmake ..
cmake --build . --config Release
```

## Prerequisites

### Windows

- Visual Studio 2015+ or MinGW-w64
- CMake 3.10+ (optional)

### Linux

- GCC 5.0+ or Clang 3.8+
- Make
- CMake 3.10+ (optional)

### macOS

- Xcode Command Line Tools (`xcode-select --install`)
- GCC or Clang
- CMake 3.10+ (optional)

## Build Methods

### Method 1: Build Scripts (Recommended for Beginners)

#### Windows (MSVC)

```bash
cd build
build.bat
```

**What it does:**

- Compiles all `.c` files with MSVC
- Creates `chess_engine.exe`
- Optimizes with `/O2` flag
- Stores object files in `build/` directory

#### Linux/macOS

```bash
cd build
chmod +x build.sh
./build.sh
```

**What it does:**

- Compiles with GCC/Clang
- Uses `-O3` and `-march=native` for optimization
- Enables link-time optimization (`-flto`)
- Creates `chess_engine` executable

### Method 2: Make (Traditional)

```bash
cd build
make
```

**Features:**

- Incremental compilation (only changed files recompiled)
- Automatic dependency detection
- Parallel build support: `make -j4`
- Clean with: `make clean`

**Custom targets:**

```bash
make clean          # Remove object files
make rebuild        # Clean + build
make release        # Optimized build
make debug          # Debug build with symbols
```

### Method 3: CMake (Cross-Platform)

**Initial setup:**

```bash
mkdir build_cmake
cd build_cmake
cmake ..
```

**Build:**

```bash
cmake --build . --config Release
```

**Or use platform-specific generators:**

Windows (Visual Studio):

```bash
cmake -G "Visual Studio 16 2019" ..
cmake --build . --config Release
```

Linux (Unix Makefiles):

```bash
cmake -G "Unix Makefiles" ..
make -j4
```

macOS (Xcode):

```bash
cmake -G "Xcode" ..
cmake --build . --config Release
```

**Installation:**

```bash
cmake --install . --prefix /usr/local
```

This installs to standard locations:

- Binary: `/usr/local/bin/chess_engine`
- Headers: `/usr/local/include/`
- Documentation: `/usr/local/share/doc/chess_engine/`

### Method 4: Manual Compilation

**Single command compilation:**

Windows (MSVC):

```bash
cl /O2 /W4 src/*.c /Fe:chess_engine.exe
```

Windows (MinGW):

```bash
gcc -O3 -march=native -flto -o chess_engine.exe src/*.c -I./include -lm
```

Linux/macOS:

```bash
gcc -O3 -march=native -flto -o chess_engine src/*.c -I./include -lm -lpthread
```

Clang:

```bash
clang -O3 -march=native -flto -o chess_engine src/*.c -I./include -lm -lpthread
```

## Compiler Flags Explained

### Optimization Flags

| Flag  | Effect                  | Use Case      |
| ----- | ----------------------- | ------------- |
| `-O0` | No optimization         | Debugging     |
| `-O1` | Basic optimization      | Balance       |
| `-O2` | Good optimization       | Recommended   |
| `-O3` | Aggressive optimization | Maximum speed |
| `-Os` | Optimize for size       | Embedded      |

### Architecture Flags

| Flag            | Effect                    |
| --------------- | ------------------------- |
| `-march=native` | CPU-specific optimization |
| `-march=x86-64` | Portable 64-bit x86       |
| `-march=core2`  | Older CPU compatibility   |

### Advanced Flags

| Flag          | Purpose                              |
| ------------- | ------------------------------------ |
| `-flto`       | Link-time optimization (+3-5% speed) |
| `-ffast-math` | Fast math (may lose precision)       |
| `-fPIC`       | Position-independent code            |
| `-g`          | Debug symbols                        |
| `-DNDEBUG`    | Disable assertions                   |

## Debug Build

To compile with debugging symbols:

**Using build.sh:**

```bash
DEBUG=1 ./build.sh
```

**Using Make:**

```bash
make debug
```

**Manual:**

```bash
gcc -g -O0 -o chess_engine_debug src/*.c -I./include -lm -lpthread
```

Then debug with:

```bash
gdb ./chess_engine_debug
```

## Performance Build

Maximum optimization for tournament play:

```bash
gcc -O3 -march=native -flto -fno-strict-aliasing \
    -o chess_engine_fast src/*.c -I./include -lm -lpthread
```

Expected performance: ~345,000 nodes/second

## Verification After Build

### Check Executable

```bash
# Windows
dir chess_engine.exe

# Linux/macOS
ls -lh chess_engine
```

Expected size: ~200-300 KB

### Test Basic Functionality

```bash
# Start engine
./chess_engine

# Type: uci
# Should respond with engine info

# Type: isready
# Should respond with: readyok

# Type: quit
# Should exit cleanly
```

### Test with Position

```
position startpos
go depth 10
```

Should search and output best move in UCI format.

## Cross-Compilation

### Compile Windows Binary on Linux

```bash
i686-w64-mingw32-gcc -O3 -o chess_engine.exe src/*.c -I./include
```

### Compile Linux Binary on Windows

Use Windows Subsystem for Linux (WSL):

```bash
wsl
./build/build.sh
```

## Static vs Dynamic Linking

### Fully Static Build

```bash
gcc -O3 -static -o chess_engine_static src/*.c -I./include -lm
```

Advantages:

- Single executable
- Portable across systems
- No dependency issues

Disadvantages:

- Larger binary size
- Library updates require recompilation

### Dynamic Build (Default)

Standard compilation with shared libraries. Smaller binary, requires system libraries.

## Continuous Integration

### GitHub Actions Example

```yaml
name: Build
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build
        run: |
          cd build
          ./build.sh
```

## Troubleshooting

### Error: "gcc not found"

**Windows:** Install MinGW-w64 or use MSVC  
**Linux:** `sudo apt-get install build-essential`  
**macOS:** `xcode-select --install`

### Error: "cannot find -lm"

Missing math library. Solution:

- Ubuntu/Debian: `sudo apt-get install libc6-dev`
- Add `-lm` flag at end of compile command

### Error: "cannot find -lpthread"

Missing pthread library. Solution:

- Ubuntu/Debian: `sudo apt-get install libc6-dev`
- Add `-lpthread` flag

### Build is Slow

- Use parallel build: `make -j4` (use 4 cores)
- Check disk space available
- Disable LTO for faster builds (remove `-flto`)

### "Undefined reference to" Errors

Missing source files. Check:

1. All `.c` files are listed in build configuration
2. All necessary header paths included with `-I`
3. No circular dependencies

### Performance is Lower than Expected

Check:

1. Using `-O3` or `-O2` (not `-O0`)
2. Using `-march=native` for CPU optimization
3. Disabled debug symbols in release build
4. Rebuilt with `make clean && make`

## Benchmarking After Build

Measure search speed:

```bash
# In engine (via UCI):
position startpos
go depth 15

# Check nodes per second reported
# Should be ~300k-400k nodes/second
```

## Recommended Build Settings

### For Development

```bash
gcc -g -O2 -march=native -o chess_engine src/*.c -I./include -lm -lpthread
```

**Benefits:** Fast compilation, debuggable, reasonable speed

### For Testing

```bash
gcc -O3 -march=native -flto -o chess_engine src/*.c -I./include -lm -lpthread
```

**Benefits:** Maximum speed, testing accuracy

### For Distribution

```bash
gcc -O3 -march=x86-64 -flto -static -o chess_engine_portable src/*.c \
    -I./include -lm -lpthread
```

**Benefits:** Portable, fast, single executable

## Advanced Configuration

### Custom Parameters

Edit `assets/tuned_params.txt` before building:

```
HISTORY_MAX=500
COUNTER_MOVE_HISTORY_MAX=500
FUTILITY_MARGIN_BASE=100
```

Recompile to use new parameters.

### Conditional Features

Uncomment in `include/types.h`:

```c
#define ENABLE_TABLEBASE      1
#define ENABLE_THREADING      1
#define ENABLE_OPENING_BOOK   1
```

Then rebuild.

## Size Optimization

For embedded systems:

```bash
gcc -Os -Ofast -march=native -o chess_engine_small src/*.c \
    -I./include -lm -lpthread -ffunction-sections -fdata-sections \
    -Wl,--gc-sections
```

Results in ~100-150 KB executable.

---

**Last Updated:** 2024  
**Engine Version:** 2.0
