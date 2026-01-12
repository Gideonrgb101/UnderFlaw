# UnderFlaw Chess Engine

A sophisticated C-based chess engine featuring **NNUE (Half-KP)** evaluation, advanced search optimizations, and game-phase awareness.

## Quick Start

### Building the Engine

**Windows (MSVC):**

```bash
cd build
build.bat
```

**Unix/Linux (GCC/Clang):**

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

### Running the Engine

The engine implements the UCI (Universal Chess Interface) protocol:

```bash
./UnderFlaw
```

## Features

### Neural Network Evaluation (NNUE) 🧠
- **Architecture**: Half-KP (40960 -> 256 -> 1)
- **Training**: Trained on self-play data using custom PyTorch pipeline
- **Performance**: Significant strength increase over classical evaluation via learned "intuition"

### Search Optimizations
- **Internal Iterative Deepening (IID)**
- **History Decay & Followup History**
- **Multi-Cut Pruning**
- **Adaptive Aspiration Windows**
- **Syzygy Tablebase Support** (via Fathom)

## Project Structure

```
C engine/
├── src/              # Source code (.c files)
├── include/          # Header files (.h files)
├── build/            # Build artifacts
├── docs/             # Documentation
├── tests/            # Test suites
├── assets/           # Neural networks and config
│   └── chess_net.nnue
├── training/         # NNUE training scripts
└── .github/          # CI/CD Workflows
```

## NNUE Training

This repository includes a full NNUE training pipeline:
1. `generate_training_data_sparse.py`: Generates massive self-play data.
2. `train_nnue_local.py`: Trains the network (CPU/GPU) with auto-resume.

## License

This project is open source.
