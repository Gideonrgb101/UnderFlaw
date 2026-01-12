# Configuration Guide

This guide explains how to configure and tune the C Chess Engine parameters.

## Parameter File Location

`assets/tuned_params.txt` contains all configurable engine parameters.

## Engine Parameters

### History Tables

#### Main History Maximum

```
HISTORY_MAX=500
```

- Controls the maximum value in the main history table
- Higher values = longer memory of good moves
- Range: 100-1000
- Trade-off: Larger values = slower history decay

#### Counter-Move History Maximum

```
COUNTER_MOVE_HISTORY_MAX=500
```

- Maximum value for counter-move history (move that refutes opponent's move)
- Range: 100-1000
- Typical: Same as HISTORY_MAX

#### Followup History Maximum

```
FOLLOWUP_HISTORY_MAX=500
```

- Maximum for followup history (move after opponent's move)
- Range: 100-1000
- Helps with move ordering based on position patterns

### Search Parameters

#### Futility Margin Base

```
FUTILITY_MARGIN_BASE=100
```

- Base margin in centipawns for futility pruning
- Formula: `margin = base + depth * 150`
- Higher = more pruning (faster but less accurate)
- Range: 50-200
- Typical: 100

#### Null Move Reduction Base

```
NULL_MOVE_REDUCTION_BASE=3
```

- Base reduction in half-moves for null move pruning
- Formula: `R = base + depth/6 + eval_bonus`
- Range: 2-4
- Trade-off: Higher = faster but less accurate

#### LMR (Late Move Reduction) Minimum Depth

```
LMR_MIN_DEPTH=3
```

- Minimum depth to apply LMR
- At depths < 3, all moves searched fully
- Range: 2-5
- Typical: 3

### Move Ordering

#### Internal Iterative Deepening Depth

```
IID_MIN_DEPTH=6
```

- Minimum depth to apply IID for PV nodes
- Helps find good move when no TT entry exists
- Range: 4-8
- Typical: 6

#### Killer Moves Configuration

```
KILLER_DEPTH_SCALE=1
```

- Multiplier for killer move bonuses by depth
- Higher = deeper killers more valuable
- Range: 0.5-2.0
- Typical: 1.0

### Time Management

#### Soft Time Limit

```
SOFT_TIME_LIMIT_PERCENT=80
```

- Percentage of allocated time before depth stop
- At 80%, engine can stop if good move found
- Range: 70-90
- Typical: 80

#### Hard Time Limit

```
HARD_TIME_LIMIT_PERCENT=100
```

- Percentage of allocated time to hard stop
- Engine must stop searching at this point
- Range: 100-150
- Typical: 100 (use exactly allocated time)

### Aspiration Window

#### Initial Window Size

```
ASPIRATION_WINDOW_SIZE=25
```

- Starting width of aspiration window in centipawns
- Narrower = fewer re-searches but more risky
- Range: 10-50
- Typical: 25

#### Window Growth Factor

```
ASPIRATION_GROWTH_FACTOR=2.0
```

- Multiplier when aspiration search fails
- After first fail: new size = size \* factor
- Range: 1.5-4.0
- Typical: 2.0

### Pruning Depths

#### Multi-Cut Pruning Minimum Depth

```
MULTI_CUT_MIN_DEPTH=7
```

- Minimum depth to apply multi-cut pruning
- Aggressive pruning at non-PV nodes
- Range: 5-10
- Typical: 7

#### Futility Pruning Minimum Depth

```
FUTILITY_MIN_DEPTH=3
```

- Minimum depth for futility pruning
- Don't prune at shallow depths
- Range: 1-4
- Typical: 3

### Transposition Table

#### Hash Table Size

```
HASH_SIZE_MB=256
```

- Transposition table size in megabytes
- Larger = better hit rate but more memory
- Range: 64-4096
- Typical: 256-512 MB

#### TT Bucket Size

```
TT_BUCKET_SIZE=4
```

- Number of entries per TT bucket
- 4 is typical for chess engines
- Leave at 4 (don't change)

### Game Phase Detection

#### Opening Phase Threshold

```
OPENING_PHASE_THRESHOLD=200
```

- Material value threshold for "opening" phase
- If total_material > threshold: opening phase
- Range: 150-250
- Typical: 200

#### Endgame Phase Threshold

```
ENDGAME_PHASE_THRESHOLD=64
```

- Material value threshold for "endgame" phase
- If total_material < threshold: endgame phase
- Range: 30-100
- Typical: 64

### Evaluation Tuning

#### Material Values

```
PAWN_VALUE=100
KNIGHT_VALUE=325
BISHOP_VALUE=335
ROOK_VALUE=500
QUEEN_VALUE=975
```

- Piece values in centipawns (100 = 1 pawn)
- Standard values for most engines
- Adjustment range: ±10-20 from standard

#### Positional Bonuses

```
BISHOP_PAIR_BONUS=30
ROOK_ON_7TH_BONUS=20
PASSED_PAWN_BONUS=50
```

- Small tactical bonuses in centipawns
- Fine-tune engine style (aggressive/conservative)

### Search Stability

#### Stability Window Size

```
STABILITY_ITERATION_WINDOW=32
```

- Number of recent iterations to monitor
- Larger = slower stability detection
- Typical: 32

#### Volatility Threshold

```
VOLATILITY_THRESHOLD=100
```

- Centipawns: if swing > this, position unstable
- Higher = less sensitive to fluctuations
- Range: 50-200
- Typical: 100

## Configuration Examples

### Aggressive Attacking Style

```
HISTORY_MAX=600
FUTILITY_MARGIN_BASE=80
NULL_MOVE_REDUCTION_BASE=2
BISHOP_PAIR_BONUS=50
```

Characteristics:

- Longer move history retention
- Less aggressive pruning
- More material value to pieces
- Plays sharper positions

### Conservative Positional Style

```
HISTORY_MAX=400
FUTILITY_MARGIN_BASE=150
NULL_MOVE_REDUCTION_BASE=4
BISHOP_PAIR_BONUS=20
```

Characteristics:

- Shorter history retention (adapts faster)
- More aggressive pruning
- Less piece activity bonus
- Safer, more solid positions

### Speed-Focused Build

```
FUTILITY_MARGIN_BASE=120
MULTI_CUT_MIN_DEPTH=6
NULL_MOVE_REDUCTION_BASE=4
HISTORY_MAX=300
```

Characteristics:

- Aggressive pruning for speed
- Less reliance on history
- Faster node processing
- Slightly weaker but faster

### Strength-Focused Build

```
HISTORY_MAX=500
FUTILITY_MARGIN_BASE=100
IID_MIN_DEPTH=5
HASH_SIZE_MB=512
```

Characteristics:

- Better move ordering
- More thoughtful pruning
- Larger hash table
- Slower but stronger

## Tuning Guidelines

### Testing New Parameters

1. **Single Parameter Change**

   - Change one parameter at a time
   - Play matches (10+ games) before/after
   - Expect ±20 ELO variance from luck

2. **Track Changes**

   - Keep backup of working configuration
   - Document expected effect
   - Test with multiple positions

3. **Evaluation**
   - Play against previous version
   - At least 20-50 games for reliability
   - Multiple opening books
   - Time controls: 1+0 to 10+0 seconds

### Promising Parameter Ranges

| Parameter                | Conservative | Aggressive | Typical |
| ------------------------ | ------------ | ---------- | ------- |
| HISTORY_MAX              | 300          | 700        | 500     |
| FUTILITY_MARGIN_BASE     | 50           | 200        | 100     |
| NULL_MOVE_REDUCTION_BASE | 2            | 4          | 3       |
| HASH_SIZE_MB             | 64           | 1024       | 256     |

## Performance Impact

Estimated nodes/second impact:

| Parameter Change       | Impact                |
| ---------------------- | --------------------- |
| +100 HASH_SIZE         | -5% (deeper search)   |
| -50 FUTILITY_MARGIN    | -10% (more positions) |
| +2 NULL_MOVE_REDUCTION | +15% (fewer searches) |
| +100 HISTORY_MAX       | -2% (larger tables)   |

## Advanced Tuning

### Automatic Parameter Tuning

Use `tuner.c` module to automatically optimize:

```
./chess_engine --tune test_positions.epd --iterations 100
```

Generates optimal parameters for given position set.

### Position-Specific Tuning

Optimize different parameters for:

- Opening positions (time-controlled)
- Middlegame positions (tactical)
- Endgame positions (simplified)

Example workflow:

1. Generate opening position set
2. Tune parameters specifically for opening
3. Document results
4. Create different configs for different phases

## Environment Variables

Control behavior at runtime:

```bash
# Increase search depth
DEPTH_MULTIPLIER=1.5 chess_engine

# Reduce memory usage
HASH_SIZE_MB=128 chess_engine

# Disable features
DISABLE_IID=1 chess_engine
DISABLE_HISTORY=1 chess_engine
```

## UCI Options

Engine supports UCI configuration options:

```
option name Hash type spin default 256 min 64 max 4096
option name Threads type spin default 1 min 1 max 256
option name UCI_ShowCurrLine type check default false
```

Set via UCI protocol:

```
setoption name Hash value 512
setoption name Threads value 4
```

## Configuration for Different Uses

### Tournament Play

```
HASH_SIZE_MB=512
HISTORY_MAX=500
FUTILITY_MARGIN_BASE=100
Hard time limits strictly enforced
```

### Casual Play

```
HASH_SIZE_MB=128
HISTORY_MAX=300
More lenient time controls
```

### Analysis Engine

```
HASH_SIZE_MB=2048
IID_MIN_DEPTH=4
Emphasis on position understanding
```

### Blitz/Bullet

```
FUTILITY_MARGIN_BASE=150
HISTORY_MAX=400
Multi-cut aggressive
```

## Troubleshooting Configuration

### Engine Playing Weaker

1. Check HASH_SIZE not reduced below 128
2. Verify FUTILITY_MARGIN not too aggressive
3. Ensure HISTORY_MAX > 200

### Engine Too Slow

1. Increase FUTILITY_MARGIN_BASE (50-200 range)
2. Increase NULL_MOVE_REDUCTION_BASE (up to 4)
3. Decrease HASH_SIZE if memory is issue

### Engine Crashing

1. Verify all values in sensible ranges
2. Check HASH_SIZE < available RAM
3. Ensure integer values, not floats

### Inconsistent Play

1. Verify hash table size adequate (>= 256MB)
2. Check stability parameters not too sensitive
3. Ensure aspiration window not too narrow

---

**Last Updated:** 2024  
**Engine Version:** 2.0
