# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A C++ GTO (Game Theory Optimal) poker solver for Heads-Up No-Limit Hold'em (HUNL), computing optimal strategies from preflop through river using Discounted Counterfactual Regret Minimization (DCFR).

**Current state:** Planning phase. `ProjectRefrence.md` is the master design document — consult it before starting any new module.

## Build System

The build system is CMake (not yet created). Once scaffolded, the standard workflow will be:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Tests live in `tests/` (one test file per module). Run a single test binary directly after building.

## Architecture

### Module Build Order (dependency-constrained)

```
core/cards
  └── core/hand_evaluator
  └── core/game_state
        └── abstraction/equity_calculator (+ hand_evaluator)
              └── abstraction/hand_clustering
        └── abstraction/action_abstraction
              └── tree/node (+ action_abstraction)
                    └── tree/info_set (+ hand_clustering)
                          └── tree/game_tree
                                └── solver/regret_store (uses mmap)
                                      └── solver/cfr_solver
                                            └── solver/exploitability
                                            └── subgame/action_translation (+ game_tree)
                                                  └── subgame/subgame_solver
```

### Data Flow

```
[Action Abstraction Config] ──┐
                               ├──> [Tree Builder] ──> [Explicit Game Tree]
[Card Abstraction Pipeline] ──┘                              │
  equity calc → histograms → k-means/EMD → buckets          ▼
                                                    [DCFR Solver]
                                                         │
                                                         ▼
                                                 [Blueprint Strategy]
                                                    (mmap files)
                                              ┌────────┴────────┐
                                              ▼                 ▼
                                      [Strategy Query]  [Subgame Solver]
```

### Key Design Decisions

| Area | Choice |
|------|--------|
| Algorithm | DCFR (α=1.5, β=0.0, γ=2.0) with external sampling |
| Tree storage | Flat arrays (cache-friendly); ActionNode = 12 bytes |
| Regret storage | Memory-mapped files (float32); ~8 GB total at 100bb |
| Card representation | Integers 0–51; rank = card/4, suit = card%4 |
| Hand evaluator | Lookup-table based (OMPEval recommended); target < 10 ns |
| Card abstraction | Bucket-based: 169 preflop / 500 flop / 1000 turn / 2000 river |
| Multithreading | Per-thread regret buffers, merge every ~1000 iterations |
| Starting scope | 100bb stack depth; expand to 300–500bb after baseline works |

### Performance Targets

| Component | Target |
|-----------|--------|
| Hand evaluation | < 10 ns per 7-card eval |
| Equity calculation | > 1M evals/sec/core |
| CFR on Kuhn poker | > 100K iterations/sec |
| CFR on NLHE 100bb | > 10 iterations/sec |
| Memory per info set | < 32 bytes |

### Development Phases

1. **Core Engine** — Cards, hand evaluator, game state
2. **CFR on Toy Games** — Kuhn poker (verify game value = −1/18 ± 0.001), then Leduc poker
3. **Abstraction Pipeline** — Equity histograms, k-means/EMD clustering, bucket files
4. **Full NLHE Solver** — DCFR on real game tree with checkpointing
5. **Subgame Solving** — Real-time re-solving with pseudo-harmonic action translation
6. **CLI & Polish** — Public library API, tools, optimization

Each phase must pass its unit tests before the next begins.
