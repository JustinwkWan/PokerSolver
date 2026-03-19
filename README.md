# GTO Poker Solver

A high-performance **Game Theory Optimal (GTO)** poker solver for No-Limit Hold'em, computing Nash equilibrium strategies from preflop through river using **Discounted Counterfactual Regret Minimization (DCFR)**.

Supports **2-6 players** (heads-up through 6-max), with real-time subgame solving, memory-mapped strategy storage, and a multi-threaded solving engine written entirely in C++17.

---

## Table of Contents

- [Features](#features)
- [How It Works](#how-it-works)
  - [The Core Idea](#the-core-idea)
  - [Counterfactual Regret Minimization](#counterfactual-regret-minimization)
  - [Discounted CFR](#discounted-cfr)
  - [External Sampling](#external-sampling)
  - [Card Abstraction](#card-abstraction)
  - [Action Abstraction](#action-abstraction)
  - [Suit Isomorphism](#suit-isomorphism)
  - [Subgame Solving](#subgame-solving)
  - [Multi-Player Generalization](#multi-player-generalization)
- [Architecture](#architecture)
- [Performance](#performance)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Building](#building)
  - [Running Tests](#running-tests)
- [Usage](#usage)
  - [Solving a Strategy](#solving-a-strategy)
  - [Querying a Strategy](#querying-a-strategy)
  - [Measuring Exploitability](#measuring-exploitability)
  - [Subgame Solving](#subgame-solving-1)
  - [Web UI](#web-ui)
- [Project Structure](#project-structure)
- [Development Phases](#development-phases)
- [Inspiration and References](#inspiration-and-references)
- [License](#license)

---

## Features

- **Full NLHE solver** — preflop through river, cash game format
- **2-6 player support** — heads-up through 6-max with correct blind structure
- **DCFR algorithm** — state-of-the-art discounted regret minimization with tunable parameters
- **External sampling MCCFR** — one card sample per iteration for fast convergence
- **Card abstraction** — equity histogram clustering with Earth Mover's Distance (EMD)
- **Action abstraction** — configurable pot-fraction bet sizes with automatic deduplication
- **Suit isomorphism** — 4-6x tree reduction by detecting strategically equivalent suits
- **Memory-mapped storage** — OS-managed paging so the full blueprint doesn't need to fit in RAM
- **Crash-safe checkpointing** — resume interrupted solves from the last checkpoint
- **Multi-threaded** — per-thread regret buffers with periodic merge, near-linear scaling
- **Subgame solving** — real-time re-solving with pseudo-harmonic action translation
- **Exploitability measurement** — Monte Carlo best-response computation
- **CLI and HTTP API** — command-line tool and REST server with async job tracking
- **Verified on toy games** — Kuhn poker converges to game value -1/18, Leduc poker converges

---

## How It Works

### The Core Idea

A GTO strategy is one where no opponent can exploit you — no matter how they play, they cannot increase their expected winnings against you. In two-player zero-sum games, this corresponds to a **Nash equilibrium**.

Computing a Nash equilibrium for poker is hard because:
1. The game tree is enormous (~10^161 states for NLHE)
2. Players have **imperfect information** (hidden hole cards)
3. Strategies must account for every possible private hand at every decision point

The solver attacks this by:
1. **Abstracting** the game (grouping similar hands into buckets, limiting bet sizes)
2. **Building** an explicit game tree over the abstract game
3. **Iterating** a self-play algorithm (CFR) that converges to equilibrium
4. **Refining** specific situations in real-time via subgame solving

### Counterfactual Regret Minimization

CFR is the family of algorithms behind all modern poker solvers. The key insight is:

> If you track how much you **regret** not taking each action, and then play proportionally to your positive regrets, your average strategy converges to a Nash equilibrium.

At each decision point (called an **information set** — the combination of your private cards and the public game history):

1. Compute a strategy proportional to positive cumulative regrets (**regret matching**)
2. Play according to that strategy
3. For each action you could have taken, compute the **counterfactual value** — what you would have won if you'd taken that action while everything else stayed the same
4. Update regrets: `regret[action] += counterfactual_value[action] - node_value`
5. Accumulate the strategy for later averaging

After enough iterations, the **average** of all strategies played converges to equilibrium.

### Discounted CFR

Standard CFR weights all iterations equally, but early iterations use bad strategies and produce noisy regrets. **DCFR** (Brown & Sandholm, 2019) discounts older data:

```
positive_regret_weight = t^alpha / (t^alpha + 1)     // alpha = 1.5
negative_regret_weight = t^beta  / (t^beta + 1)      // beta  = 0.0
strategy_weight        = (t / (t+1))^gamma            // gamma = 2.0
```

With `beta = 0.0`, negative regrets are immediately zeroed out — actions proven bad are forgotten instantly. Positive regrets are discounted gently. This gives **dramatically faster convergence** than vanilla CFR (often 10-100x fewer iterations needed).

### External Sampling

Full tree traversal is too expensive for large games. **External sampling** (a variant of Monte Carlo CFR) works as follows:

1. **Sample** one set of cards (hole cards for all players + full board)
2. **Pick a traverser** — this player explores ALL their actions
3. **Opponents sample** — non-traverser players follow the current strategy (pick one action stochastically)
4. Update regrets only for the traverser
5. Alternate the traverser each iteration

This dramatically reduces the per-iteration cost while maintaining convergence guarantees. The solver cycles through all players as the traverser.

### Card Abstraction

With ~2.6 billion distinct card combinations across all streets, storing a strategy for every exact hand is infeasible. **Card abstraction** groups strategically similar hands into **buckets**:

| Street | Buckets | Method |
|--------|---------|--------|
| Preflop | 169 | Canonical hand types (AA, AKs, AKo, ...) |
| Flop | 500 | Equity histogram clustering (k-means + EMD) |
| Turn | 1,000 | Equity histogram clustering (k-means + EMD) |
| River | 2,000 | Equity histogram clustering (k-means + L2) |

**How clustering works:**

1. For each hand on a given street, run a Monte Carlo rollout against random opponent hands to build an **equity histogram** — a distribution showing how often you win, tie, or lose
2. Hands with similar equity distributions are grouped using **k-means clustering** with **Earth Mover's Distance** (EMD) as the distance metric
3. EMD measures the "work" needed to transform one histogram into another — it captures that a hand winning 60% of the time is more similar to one winning 65% than to one winning 30%

### Action Abstraction

Real poker has a continuous bet sizing space (any amount from min-raise to all-in). The solver discretizes this into a small set of **pot-fraction bets** per street:

```
Default: {0.5x pot, 1.0x pot} + all-in on every street
```

The `ActionAbstraction` module:
- Converts pot fractions to chip amounts given the current game state
- Deduplicates bets that map to the same chip amount
- Clamps bets to legal range (min-raise to all-in)
- Always includes fold, check, call as appropriate

More bet sizes = more accurate strategy but exponentially larger tree.

### Suit Isomorphism

On many boards, two or more suits are **strategically interchangeable**. For example, on a `Ah 7d 2c` flop, diamonds and clubs are equivalent — swapping all diamonds and clubs in anyone's hand doesn't change the strategic situation.

The solver detects these isomorphisms and **canonicalizes** hands, reducing the effective number of hands to consider by 4-6x on average. This is a free accuracy improvement — no approximation is involved.

### Subgame Solving

The blueprint strategy (computed offline) uses coarse abstractions for tractability. For specific situations at play time, **subgame solving** builds a finer-grained subtree and re-solves it:

1. Start from the current game state (street, pot, stacks, action history)
2. Build a new subtree with (optionally) more bet sizes
3. Run DCFR on just this subtree
4. Return the refined strategy

When the opponent makes a bet that doesn't match any abstract action, **pseudo-harmonic action translation** maps it to a weighted combination of nearby abstract actions:

```
weight(closer_action) = (far_action - actual_bet) / (far_action - close_action)
```

This ensures smooth interpolation between abstract bet sizes.

### Multi-Player Generalization

The solver generalizes from heads-up to N-player (2-6) using the approach from **Pluribus** (Brown & Sandholm, 2019):

- Game state uses `std::array<T, kMaxPlayers>` (kMaxPlayers=6) instead of `std::vector` — trivially copyable, zero heap allocation, fits in 3 cache lines
- Blind structure: HU uses button=SB; N>2 uses SB=(BTN+1)%N, BB=(BTN+2)%N, UTG=(BTN+3)%N
- Preflop action order: UTG first (N>2) or SB first (HU)
- Postflop action order: first active player after button
- Terminal payoff: multi-way showdown with best-hand-wins (split on tie)
- CFR traverser cycles through all N players
- Exploitability = sum of all players' best-response values / N

All existing HU functionality continues working with the default `num_players=2`.

---

## Architecture

```
poker-solver/
├── src/
│   ├── core/              Cards, hand evaluator (OMPEval), game state
│   ├── abstraction/       Action abstraction, equity calc, hand clustering, suit isomorphism
│   ├── tree/              Explicit game tree (flat arrays), info set manager
│   ├── solver/            DCFR engine, regret store (mmap), exploitability
│   ├── subgame/           Subgame solver, pseudo-harmonic action translation
│   ├── cli/               CLI entry point, public API implementation
│   └── server/            HTTP REST server with async job tracking
├── include/
│   └── poker_solver/      Public library API header (solver.h)
├── tests/                 16 test files, 100+ test cases
├── tools/                 Offline tools (abstraction builder)
└── CMakeLists.txt         Build system
```

### Data Flow

```
[Action Abstraction Config] ──┐
                               ├──> [Tree Builder] ──> [Explicit Game Tree]
[Card Abstraction Pipeline] ──┘         (flat arrays, ~millions of nodes)
  equity calc → histograms                        │
  → k-means/EMD → buckets                        ▼
                                         [DCFR Solver]
                                     (external sampling, multi-threaded)
                                              │
                                              ▼
                                      [Blueprint Strategy]
                                      (memory-mapped files)
                                    ┌─────────┴─────────┐
                                    ▼                   ▼
                            [Strategy Query]    [Subgame Solver]
                            (CLI / HTTP API)    (real-time refine)
```

### Key Design Decisions

| Area | Choice | Why |
|------|--------|-----|
| Language | C++17 | Billions of iterations need native speed |
| Algorithm | DCFR (alpha=1.5, beta=0.0, gamma=2.0) | Best convergence, tunable |
| Tree storage | Flat arrays | Cache-friendly, direct indexing |
| Regret storage | Memory-mapped files (float32) | OS paging, crash-safe, ~8 GB at 100bb |
| Card encoding | uint8_t 0-51 (rank=card/4, suit=card%4) | Compact, branchless |
| Hand evaluator | OMPEval lookup tables | < 10 ns per 7-card eval |
| N-player arrays | `std::array<T, 6>` (not `std::vector`) | Zero-alloc copy on every `apply()` |
| Multithreading | Per-thread regret buffers, merge every ~1000 iterations | No lock contention |
| Chip convention | 1 unit = 0.5 BB (SB=1, BB=2, 100bb=200 chips) | Integer arithmetic, no fractions |

---

## Performance

| Component | Target | Notes |
|-----------|--------|-------|
| Hand evaluation | < 10 ns/eval | OMPEval lookup tables |
| Equity calculation | > 1M evals/sec/core | Monte Carlo rollout |
| CFR on Kuhn poker | > 100K iterations/sec | Verified: game value = -1/18 |
| CFR on NLHE 100bb | > 10 iterations/sec | With external sampling |
| Memory per info set | < 32 bytes | float32 regrets + strategy |
| Stress test | 10M random hands | Zero crashes or assertion failures |

---

## Getting Started

### Prerequisites

- **C++17 compiler** — GCC 9+, Clang 10+, or Apple Clang 12+
- **CMake 3.20+**
- **Git** — for fetching dependencies
- **POSIX system** — Linux or macOS (uses `mmap`, `msync` for memory-mapped storage)

All other dependencies are fetched automatically by CMake:
- [OMPEval](https://github.com/zekyll/OMPEval) — fast hand evaluator
- [Google Test](https://github.com/google/googletest) — unit testing
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — HTTP server
- [nlohmann/json](https://github.com/nlohmann/json) — JSON serialization

### Building

```bash
git clone https://github.com/yourusername/PokerSolver.git
cd PokerSolver

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)    # Linux
make -j$(sysctl -n hw.ncpu)  # macOS
```

The first build takes a few minutes while CMake fetches and compiles dependencies.

**Build types:**
- `Release` — optimized (`-O3 -march=native`), for solving
- `Debug` — with AddressSanitizer and UndefinedBehaviorSanitizer, for development

### Running Tests

```bash
cd build

# Run all tests
ctest --output-on-failure

# Or run individual test binaries
./tests/test_game_state        # Game state logic (including 10M random hands)
./tests/test_kuhn              # Kuhn poker convergence (game value = -1/18)
./tests/test_leduc             # Leduc poker convergence
./tests/test_cfr_solver        # DCFR solver correctness
./tests/test_exploitability    # Best-response computation
./tests/test_game_tree         # Tree construction (HU and 3-player)
./tests/test_regret_store      # Memory-mapped storage
./tests/test_subgame_solver    # Subgame re-solving
./tests/test_hand_eval         # Hand evaluation
./tests/test_cards             # Card encoding
./tests/test_action_abstraction
./tests/test_action_translation
./tests/test_info_set
./tests/test_equity
./tests/test_clustering
./tests/test_suit_isomorphism
```

All 100+ tests should pass. The `test_game_state` and `test_leduc` tests take the longest (~8s and ~76s respectively).

---

## Usage

The solver binary is at `build/src/cli/poker-solver`.

### Solving a Strategy

```bash
# Solve heads-up, 100bb deep, 1M iterations
poker-solver solve --stack 100 --iterations 1000000 --output data/strategy

# Solve 6-max, 50bb deep, 100K iterations
poker-solver solve --players 6 --stack 50 --iterations 100000 --output data/6max_strategy

# Resume from a checkpoint
poker-solver solve --stack 100 --iterations 500000 --resume data/strategy/checkpoint_500000

# Tune DCFR parameters
poker-solver solve --stack 100 --iterations 1000000 --alpha 1.5 --beta 0.0 --gamma 2.0
```

**Options:**
| Flag | Default | Description |
|------|---------|-------------|
| `--stack N` | 100 | Stack depth in big blinds |
| `--players N` | 2 | Number of players (2-6) |
| `--iterations N` | 1000000 | DCFR iterations |
| `--output DIR` | data/strategy | Output directory |
| `--resume DIR` | — | Resume from checkpoint |
| `--checkpoint N` | 100000 | Iterations between checkpoints |
| `--alpha F` | 1.5 | DCFR positive regret discount |
| `--beta F` | 0.0 | DCFR negative regret discount |
| `--gamma F` | 2.0 | DCFR strategy weight discount |
| `--seed N` | 42 | Random seed |

### Querying a Strategy

```bash
# What should I do with AhKs preflop?
poker-solver query --strategy data/strategy --hand AhKs

# What should I do with AhKs on a Td9c2h flop after a raise-call?
poker-solver query --strategy data/strategy --hand AhKs --board Td9c2h --history rc

# 6-max query
poker-solver query --strategy data/6max_strategy --hand AhKs --players 6
```

**History notation:** `f`=fold, `k`=check, `c`=call, `b`/`r`=bet/raise

### Measuring Exploitability

```bash
# How close is the strategy to Nash equilibrium?
poker-solver exploit --strategy data/strategy --samples 100000

# 6-max exploitability
poker-solver exploit --strategy data/6max_strategy --players 6 --samples 50000
```

Exploitability is measured in chips per hand. A perfect Nash equilibrium has exploitability = 0. Lower is better.

### Subgame Solving

```bash
# Re-solve a specific spot with more iterations
poker-solver subgame --hand AhKs --board Td9c2h7d --history rcc --iterations 10000
```

### Web UI

```bash
# Launch the HTTP server
poker-solver ui --port 8080 --static-dir web/dist
```

Provides a REST API at `http://localhost:8080` with endpoints for solving, querying, and measuring exploitability. Jobs run asynchronously with progress tracking.

---

## Project Structure

### Module Map

```
core/cards
  └── core/hand_evaluator
  └── core/game_state
        └── abstraction/equity_calculator (+ hand_evaluator)
              └── abstraction/hand_clustering
        └── abstraction/action_abstraction
              └── tree/game_tree (+ action_abstraction)
                    └── tree/info_set (+ hand_clustering)
                          └── solver/regret_store
                                └── solver/cfr_solver
                                      └── solver/exploitability
                                      └── subgame/action_translation (+ game_tree)
                                            └── subgame/subgame_solver
```

### Key Files

| File | Lines | Purpose |
|------|-------|---------|
| `src/core/game_state.h/cpp` | ~330 | N-player game state with immutable apply() |
| `src/core/hand_evaluator.h/cpp` | ~50 | OMPEval wrapper, < 10ns per eval |
| `src/core/cards.h/cpp` | ~200 | Card encoding, deck, canonical preflop hands |
| `src/abstraction/action_abstraction.h/cpp` | ~150 | Pot-fraction bet sizing |
| `src/abstraction/equity_calculator.h/cpp` | ~200 | Monte Carlo equity distributions |
| `src/abstraction/hand_clustering.h/cpp` | ~300 | k-means with L2 and EMD |
| `src/abstraction/suit_isomorphism.h/cpp` | ~250 | Suit equivalence detection |
| `src/tree/game_tree.h/cpp` | ~110 | Flat-array explicit game tree |
| `src/tree/info_set.h/cpp` | ~150 | (node, bucket) -> flat info set ID mapping |
| `src/solver/regret_store.h/cpp` | ~280 | Memory-mapped regret/strategy storage |
| `src/solver/cfr_solver.h/cpp` | ~460 | DCFR with external sampling, multi-threaded |
| `src/solver/exploitability.h/cpp` | ~150 | Monte Carlo best-response |
| `src/subgame/subgame_solver.h/cpp` | ~70 | Nested DCFR on subtrees |
| `src/subgame/action_translation.h/cpp` | ~100 | Pseudo-harmonic bet mapping |
| `include/poker_solver/solver.h` | ~100 | Public API |
| `src/cli/main.cpp` | ~340 | CLI with subcommands |
| `src/cli/solver_api.cpp` | ~410 | API implementation |
| `src/server/http_server.h/cpp` | ~300 | REST server with async jobs |

---

## Development Phases

The project was built incrementally, each phase verified before the next:

| Phase | Name | Status | Key Milestone |
|-------|------|--------|---------------|
| 1 | Core Engine | Done | Cards, hand evaluator (< 10ns), game state |
| 2 | CFR on Toy Games | Done | Kuhn poker game value = -1/18 +/- 0.001 |
| 3 | Abstraction Pipeline | Done | Equity histograms, k-means/EMD clustering, suit isomorphism |
| 4 | Full NLHE Solver | Done | DCFR on real game tree with checkpointing |
| 5 | Subgame Solving | Done | Real-time re-solving with action translation |
| 6 | CLI & HTTP Server | Done | Public API, web UI server |
| 7-9 | GUI & Polish | Done | Web interface, visualization |
| 10 | N-Player (2-6) | Done | 6-max support, Pluribus-style generalization |

---

## Inspiration and References

This project draws from decades of poker AI research. Here are the key papers, projects, and ideas that informed the design.

### Foundational Papers

- **Zinkevich et al. (2007)** — *"Regret Minimization in Games with Incomplete Information"*
  Introduced CFR. The core algorithm behind every modern poker solver.
  [Paper](http://martin.zinkevich.org/publications/regretpoker.pdf)

- **Lanctot et al. (2009)** — *"Monte Carlo Sampling for Regret Minimization in Extensive Games"*
  Introduced MCCFR variants (outcome sampling, external sampling, chance sampling). External sampling is what this solver uses — the traverser explores all actions while opponents sample one.
  [Paper](https://papers.nips.cc/paper/2009/hash/00411460f7c92d2124a67ea0f4cb5f85-Abstract.html)

- **Brown & Sandholm (2019)** — *"Solving Imperfect-Information Games via Discounted Regret Minimization"*
  Introduced DCFR. The key insight: discount old regrets using `t^alpha / (t^alpha + 1)` weights. With alpha=1.5, beta=0.0, gamma=2.0, convergence is dramatically faster than vanilla CFR. This is the exact algorithm implemented here.
  [Paper](https://arxiv.org/abs/1809.04040)

### Poker AI Systems

- **Libratus (Brown & Sandholm, 2017)** — *"Superhuman AI for heads-up no-limit poker"*
  First superhuman HU NLHE AI. Introduced blueprint + real-time subgame solving + safe subgame solving. Ran on a supercomputer for 15M core-hours.
  [Paper](https://www.science.org/doi/10.1126/science.aao1733)

- **Pluribus (Brown & Sandholm, 2019)** — *"Superhuman AI for multiplayer poker"*
  Extended to 6-player. Key innovations: depth-limited search in multiplayer (where safe subgame solving doesn't apply), and the idea that you only need a coarse blueprint + real-time search. Our N-player generalization follows this approach.
  [Paper](https://www.science.org/doi/10.1126/science.aay2400)

- **DeepStack (Moravcik et al., 2017)** — *"DeepStack: Expert-level artificial intelligence in heads-up no-limit poker"*
  Used neural networks for value estimation instead of explicit abstraction. A different approach from the tabular method used here.
  [Paper](https://www.science.org/doi/10.1126/science.aam6960)

### Abstraction and Bucketing

- **Johanson et al. (2013)** — *"Evaluating State-Space Abstractions in Extensive-Form Games"*
  Comprehensive analysis of abstraction quality metrics. Shows that Earth Mover's Distance (EMD) is superior to k-means with L2 for poker hand clustering.
  [Paper](https://poker.cs.ualberta.ca/publications/AAMAS13-abstraction.pdf)

- **Gilpin & Sandholm (2007)** — *"Lossless Abstraction of Imperfect Information Games"*
  Introduced suit isomorphism as lossless abstraction. Swapping strategically equivalent suits reduces the game tree by 4-6x with zero accuracy loss.
  [Paper](https://www.cs.cmu.edu/~sandholm/lossless.jacm.pdf)

### Action Translation

- **Schnizlein, Bowling & Szafron (2009)** — *"Probabilistic State Translation in Extensive Games with Large Action Spaces"*
  Introduced pseudo-harmonic action translation for mapping off-abstraction bet sizes to nearby abstract actions. The formula: `P(A) = (B - x) / (B - A)` provides smooth interpolation.
  [Paper](https://poker.cs.ualberta.ca/publications/aaai09-schnizlein.pdf)

### Open-Source Inspirations

- **[OMPEval](https://github.com/zekyll/OMPEval)** — The hand evaluator used in this project. Lookup-table based, achieves < 10ns per 7-card evaluation.

- **[OpenSpiel](https://github.com/deepmind/open_spiel)** — Google DeepMind's framework for research in games. Contains reference CFR implementations in C++ and Python. Useful for understanding the algorithm but not optimized for poker specifically.

- **[apcode/pluribus-poker-AI](https://github.com/apcode/pluribus-poker-AI)** — An open-source attempt at implementing Pluribus. Has a working 6-max Python engine but the AI module is unfinished (only pseudocode stubs). Useful for understanding the poker engine requirements but not the AI algorithms.

### Textbooks and Surveys

- **Nisan et al.** — *"Algorithmic Game Theory"* (Chapter 4: Computing Equilibria)
  Excellent background on Nash equilibrium computation in extensive-form games.

- **Burch (2017)** — *"Time and Space: Why Imperfect Information Games are Hard"* (PhD thesis)
  Deep dive into the computational challenges of poker solving, including memory management, sampling, and abstraction.
  [Thesis](https://era.library.ualberta.ca/items/23b4f53e-4ee4-4544-a10a-1a82a2b2e62d)

---

## License

This project is provided as-is for educational and research purposes.
