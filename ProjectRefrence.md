# GTO Poker Solver: Project Reference

Last updated: March 17, 2026

This is the master reference document for the solver project. All design decisions, tradeoffs, best practices, and phase tracking live here. Come back to this before starting any new module.

---

## Design Decisions (Locked In)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Game variant | Heads-up NLHE | Most reference material, benchmarks, open-source code |
| Game scope | Full preflop-to-river | Harder than postflop-only but produces a complete solver |
| Format | Cash game only | No ICM, no tournament logic |
| Stack depth | Up to 300-500bb | Ambitious. See risk notes below |
| Language | C++ | Performance-critical. Billions of iterations need native speed |
| Abstraction | Tabular (bucket-based) | Proven, debuggable, no GPU dependency |
| CFR variant | DCFR with external sampling | Best convergence speed, tunable parameters |
| Tree type | Explicit tree in flat arrays | Cache-friendly traversal, direct index into regret arrays |
| Storage | Memory-mapped files | OS handles paging, don't need full blueprint in RAM |
| Bet sizes | Start with 3 per action point | Keep tree small initially, increase after first working solution |
| Suit isomorphism | Implement at chance nodes (Phase 3) | 4-6x average tree reduction with zero accuracy loss. Proven in Shark 2.0 |
| Showdown evaluation | Two-pointer sweep on sorted hands | O(n+m) vs O(n×m) naive. Required for competitive CFR iteration speed |
| Interface | CLI + clean library API | GUI later if needed |

### Risk: 300-500bb Stack Depth

Deep stacks create much larger game trees than 100bb because:

- More meaningful bet sizes exist relative to the pot (a 2x pot bet at 100bb is often all-in, at 500bb it's a normal bet)
- More raises are possible before all-in is reached
- The tree branches more at every decision point

Mitigation strategies:

- Start development and testing at 100bb. Get everything working there first.
- Use coarser action abstraction at deeper stacks (fewer bet sizes)
- Use more aggressive card abstraction (fewer buckets) to compensate for larger trees
- Consider solving preflop at full depth but only solving postflop subgames to 200bb initially, then extending
- Profile memory usage early. If a 500bb tree with 3 bet sizes blows past 256GB, we need to know before building the full pipeline.

---

## Architecture Overview

```
poker-solver/
├── src/
│   ├── core/           # Card representation, hand evaluation, game rules
│   ├── abstraction/    # Equity calculation, hand clustering, bet tree config
│   ├── tree/           # Explicit game tree, node types, info set mapping
│   ├── solver/         # DCFR engine, regret storage, exploitability
│   ├── subgame/        # Real-time re-solving, action translation
│   └── cli/            # Command-line interface
├── include/            # Public library API headers
├── tests/              # Unit tests per module
├── tools/              # Standalone offline tools (abstraction, querying)
├── benchmarks/         # Performance benchmarks per module
├── data/               # Lookup tables, precomputed data
├── docs/               # This file and other documentation
└── scripts/            # Build/run helper scripts
```

### Data Flow

```
[Action Abstraction Config] ──┐
                               ├──> [Tree Builder] ──> [Explicit Game Tree]
[Card Abstraction Pipeline] ──┘                              │
  (equity calc -> histograms -> k-means/EMD -> buckets)      │
                                                             ▼
                                                    [DCFR Solver]
                                                         │
                                                         ▼
                                                 [Blueprint Strategy]
                                                    (mmap files)
                                                         │
                                            ┌────────────┴────────────┐
                                            ▼                         ▼
                                    [Strategy Query]          [Subgame Solver]
                                    (CLI tool)                (real-time refine)
```

### Module Dependencies

Build order is constrained by these dependencies:

1. `core/cards` (no dependencies)
2. `core/hand_evaluator` (depends on cards)
3. `core/game_state` (depends on cards)
4. `abstraction/equity_calculator` (depends on hand_evaluator, game_state)
5. `abstraction/hand_clustering` (depends on equity_calculator)
6. `abstraction/action_abstraction` (depends on game_state)
7. `tree/node` (depends on game_state, action_abstraction)
8. `tree/info_set` (depends on node, hand_clustering)
9. `tree/game_tree` (depends on node, info_set)
10. `solver/regret_store` (depends on info_set, uses mmap)
11. `solver/cfr_solver` (depends on game_tree, regret_store)
12. `solver/exploitability` (depends on cfr_solver)
13. `subgame/action_translation` (depends on game_tree)
14. `subgame/subgame_solver` (depends on cfr_solver, action_translation)

---

## Phase Plan

### Phase 1: Core Engine (Weeks 1-3)

**Goal:** Card representation, hand evaluator, game state management. All tested.

**Files:**
- `src/core/cards.h/.cpp`
- `src/core/hand_evaluator.h/.cpp`
- `src/core/game_state.h/.cpp`
- `tests/test_hand_eval.cpp`
- `tests/test_game_state.cpp`

**Cards module details:**

- Represent cards as integers 0-51. Rank = card / 4, Suit = card % 4.
- Deck as a 52-element array with Fisher-Yates shuffle.
- Canonical hand representation for preflop: 169 unique hands (13 pairs + 78 suited + 78 offsuit). Use a lookup table mapping any 2-card combo to its canonical index.
- Suit isomorphism: preflop, suits are interchangeable. Postflop, suits matter only relative to the board.

**Hand evaluator details:**

- Use a lookup-table based evaluator. The 2+2 evaluator (36MB table, fastest possible) or OMPEval (200KB table via perfect hashing, nearly as fast) are the two best options.
- Evaluate 7-card hands (2 hole + 5 board) returning a 16-bit rank where higher is better.
- The evaluator will be called billions of times during abstraction computation. Benchmark target: < 10 nanoseconds per evaluation on modern hardware.
- Consider integrating an existing library (OMPEval) rather than writing from scratch. The algorithm is well-understood but fiddly to implement and easy to get subtly wrong./

**Best practice:** Write a test that enumerates all 133,784,560 possible 7-card hands and verifies the rank distribution matches known hand frequencies (e.g., exactly 4,324 four-of-a-kind hands out of the total).

**Game state details:**

- Track: hole cards per player, board cards, pot size, stack sizes, current street, action history, whose turn it is.
- Legal action generation: fold, check, call, bet/raise (with size). At this stage, allow arbitrary bet sizes. The action abstraction module will constrain this later.
- State transitions must be deterministic and reversible (undo support helps with tree traversal).
- NLHE-specific rules: blinds, button position, betting order per street (preflop: UTG acts first; postflop: OOP acts first).

**Deliverable:** Run 10 million random games with random legal actions, verify no crashes, no illegal states, and that showdown evaluation works correctly.

**Status:** [x] Complete

---

### Phase 2: CFR on Toy Games (Weeks 4-6)

**Goal:** Validate CFR logic on Kuhn and Leduc poker before touching NLHE.

**Files:**
- `tests/test_kuhn.cpp`
- `tests/test_leduc.cpp`
- Temporary solver code (will be refactored in Phase 4)

**Kuhn poker (3-card game):**

- 3 cards (J, Q, K), 2 players, 1 card each, ante of 1 chip.
- Actions: check or bet 1 chip.
- 12 information sets total.
- Known Nash equilibrium: game value = -1/18 for player 1.
- Implement vanilla CFR first (not DCFR). Full tree traversal every iteration. This should converge in ~10,000 iterations to exploitability < 0.001.
- Player 1 with J should check 100% and fold to a bet ~67% of the time. Player 1 with K should bet ~100% after opponent checks. Use these as sanity checks.

**Leduc poker (6-card game):**

- Deck: J, Q, K in two suits (6 cards). 2 players, 1 hole card each, 1 community card.
- Two betting rounds with fixed bet sizes (2 chips round 1, 4 chips round 2). Max 2 raises per round.
- ~936 information sets.
- No closed-form equilibrium, but exploitability should converge below 0.01 within 100,000 iterations.
- This is the first test with community cards and multiple betting rounds.

**Best practices:**

- Represent information sets as strings: concatenate player's card + action history. e.g., "K:cb" means player holds K, action sequence was check-bet.
- Store regrets and strategies in hash maps for toy games. We'll switch to flat arrays for NLHE.
- Implement exploitability calculation (best-response computation) for these small games. This is your ground truth for whether the solver is working.

**Tradeoff: Vanilla CFR vs DCFR for toy games.** Use vanilla CFR here because it's simpler and the games are small enough that convergence speed doesn't matter. DCFR adds complexity (three discount parameters) that isn't worth debugging on a 12-info-set game.

**Deliverable:** Kuhn converges to known equilibrium. Leduc converges to low exploitability. Both verified numerically.

**Status:** [ ] Not started

---

### Phase 3: Abstraction Pipeline (Weeks 7-11)

**Goal:** Compute card buckets for all four streets of NLHE. This is the most computationally expensive offline step.

**Files:**
- `src/abstraction/equity_calculator.h/.cpp`
- `src/abstraction/hand_clustering.h/.cpp`
- `src/abstraction/action_abstraction.h/.cpp`
- `tests/test_equity.cpp`
- `tools/build_abstraction.cpp`

**Equity calculator:**

- For a given (hole cards, board) combo, estimate equity against a uniform random opponent range via Monte Carlo sampling.
- Sample N random opponent hands and remaining board cards, evaluate both, count wins/ties/losses.
- N = 1,000-5,000 samples per hand gives sufficient accuracy for clustering. More is better but slower.
- This is embarrassingly parallel. Use std::thread or OpenMP to distribute across cores.
- Precompute a full equity table for the river (no more cards to come, so equity is deterministic given known cards). Only flop and turn need Monte Carlo.

**Equity histograms (for flop and turn):**

- Instead of a single equity number, compute a distribution of equities across possible future boards.
- For a flop hand: sample many possible turn+river runouts, compute equity for each, build a histogram with K bins (K = number of buckets on the next street).
- For the turn: sample river cards, compute equity, histogram over river buckets.
- This is the input to the clustering algorithm.

**Hand clustering (k-means with EMD):**

- River: cluster hands by equity value (1D). k-means with L2 distance works fine here.
- Turn: cluster hands by equity histograms (distribution over river buckets). Use Earth Mover's Distance as the metric. EMD in 1D has a linear-time exact solution (scan and accumulate).
- Flop: cluster by histograms over turn buckets. Now EMD is higher-dimensional and expensive. Use the fast EMD approximation from Sandholm's 2014 paper: sort histogram bins, scan, accumulate differences.
- Preflop: no abstraction (169 canonical hands). Every distinct preflop hand gets its own bucket.

**Starting bucket counts:**

| Street | Buckets | Notes |
|--------|---------|-------|
| Preflop | 169 | No abstraction, all canonical hands |
| Flop | 500 | Start conservative. 1000-2000 is better but slower |
| Turn | 1000 | |
| River | 2000 | Equity-only clustering (simplest) |

These numbers directly determine memory usage and solve time. Double the buckets roughly quadruples the tree size (because both players' buckets multiply).

**Action abstraction:**

- Define bet sizes as fractions of pot. Starting with 3 sizes:
  - 0.5x pot (small), 1.0x pot (medium), all-in (large)
  - Plus fold, check, call as always-available actions
- At 100bb with 3 bet sizes, the tree is manageable. At 500bb, the tree gets much deeper because more raises fit before all-in.
- Store the bet tree configuration in a simple config file (JSON or similar) so it's easy to change without recompiling.

**Suit isomorphism (at chance nodes):**

- At chance nodes (turn/river card deals), detect when two or more suits have identical strategic implications — i.e., swapping those suits across both players' hands and the board produces an equivalent game state.
- When suits are isomorphic, only solve one "representative" suit; copy results to the symmetric cases via a swap permutation table.
- **How to detect:** At each chance node, check whether any pair of suits can be swapped without changing any player's hand category or draw potential given the current board. On a rainbow flop with no flush draws, all suits not appearing on the board are interchangeable. On a two-tone flop, only the two non-board suits may be swappable.
- **Savings by board texture:**

| Board texture | Isomorphic suits | Tree reduction |
|---------------|-----------------|----------------|
| Rainbow flop (3 suits) | Up to 1 free suit | ~4-6x |
| Two-tone flop (2 suits) | Up to 2 free suits | ~2-3x |
| Monotone flop (1 suit) | None | ~1x (no savings) |
| Turn (4 cards) | Varies | ~2-4x average |
| River (5 cards) | Varies | ~1-2x average |

- **Implementation:** Add an `IsomorphismDetector` that, given a board, returns which suits are interchangeable and the permutation maps. The tree builder uses this at chance nodes to prune duplicate subtrees. The solver copies results from solved subtrees to their isomorphic equivalents after each iteration.
- **Reference implementation:** Shark 2.0's `IsomorphismComputer` in `src/solver/Isomorphism.hh` does exactly this and is a good reference.
- This is a **zero-cost accuracy optimization** — no approximation, no information loss, just avoiding redundant computation.

**Strategically distinct flops:** After applying suit isomorphism, there are only **1,755 strategically distinct flops** out of 22,100 possible flops (C(52,3)). For testing and validation, even smaller representative subsets can be used — GTO Wizard uses subsets of 25, 49, 85, or 184 representative flops that cover the major board textures. Use this to scope validation work: you don't need to test all flops, just a representative set that covers dry/wet/monotone/paired/connected textures.

**Tradeoff: EMD approximation quality.** Exact multi-dimensional EMD is O(n^3) via linear programming. The fast approximation is O(n log n) and loses some accuracy. In practice, the approximation is close enough that it doesn't measurably affect play quality. Use the approximation.

**Tradeoff: Imperfect recall.** Hands that are in the same bucket on the flop might have been distinguishable preflop. The solver "forgets" this distinction. This is standard practice and saves enormous memory. The cost is that the solver can't distinguish between, say, a hand that was strong preflop and got worse vs. one that was weak and got better. Deeper stack play (where preflop dynamics matter more) makes this tradeoff worse. Something to monitor.

**Deliverable:** A tool (`build_abstraction`) that reads a config, runs the full pipeline, and outputs bucket assignment files. Verify by spot-checking that similar hands land in the same buckets.

**Status:** [ ] Not started

---

### Phase 4: Full NLHE Solver (Weeks 12-18)

**Goal:** Build the explicit game tree for abstracted NLHE, implement DCFR with external sampling and memory-mapped storage.

**Files:**
- `src/tree/node.h/.cpp`
- `src/tree/info_set.h/.cpp`
- `src/tree/game_tree.h/.cpp`
- `src/solver/cfr_solver.h/.cpp`
- `src/solver/regret_store.h/.cpp`
- `src/solver/exploitability.h/.cpp`
- `tests/test_tree.cpp`
- `benchmarks/bench_solver.cpp`

**Tree builder:**

- Build the full game tree from root (preflop, before any actions) to all terminal nodes (fold, showdown, all-in).
- Nodes stored in flat arrays, not heap-allocated objects with pointers. This is critical for cache performance. Each node type (chance, action, terminal) gets its own array. Children are referenced by index, not pointer.
- Chance nodes represent card deals (preflop hole cards, flop, turn, river).
- Action nodes represent player decisions. Each has N children where N = number of legal actions.
- Terminal nodes store pot size and which player(s) are still in.
- Information set IDs: encode as (street, player, bucket, action_history_hash). Two nodes belong to the same info set if the player can't distinguish between them (same cards from their perspective, same actions observed).

**Node memory layout (suggested):**

```cpp
struct ActionNode {
    uint32_t info_set_id;      // Index into regret/strategy arrays
    uint32_t num_actions;       // 1-8 typically
    uint32_t children_offset;   // Index into children array
    uint8_t  player;            // 0 or 1
    uint8_t  street;            // 0-3
    uint16_t padding;
};
// 12 bytes per node. Millions of nodes = low tens of MB for tree structure.
```

**Regret store (memory-mapped):**

- Two arrays per player: cumulative_regrets[info_set_id][action] and cumulative_strategy[info_set_id][action].
- Stored as memory-mapped files. Use mmap() on Linux.
- Data type: float32 is sufficient. Don't use float64 unless you observe precision issues.
- Memory estimate: if there are 100 million info sets with 5 actions average, that's 100M * 5 * 4 bytes = 2 GB per array, ~8 GB total. This fits comfortably in RAM at 100bb but watch it at 500bb.

**DCFR implementation:**

- External sampling: on each iteration, sample one set of chance outcomes (board cards, opponent hole cards) and traverse all of the current player's actions.
- DCFR discount parameters (from Brown & Sandholm 2019):
  - alpha = 1.5 (positive regret discount: weight iteration t by t^alpha / (t^alpha + 1))
  - beta = 0.0 (negative regret discount: weight by t^beta / (t^beta + 1))
  - gamma = 2.0 (strategy contribution discount: weight by (t / (t+1))^gamma)
- These are the defaults from the paper. They work well across most games. Tune later if needed.
- Regret matching: strategy at each info set is proportional to positive cumulative regrets. If all regrets are negative, use uniform random.

**Multithreading:**

- External sampling CFR is naturally parallelizable: each thread runs independent iterations with different chance samples.
- Regret updates are the contention point. Options:
  - Lock-free atomic adds (works for float with compare-and-swap, slight overhead)
  - Per-thread regret buffers that merge periodically (more memory, no contention)
  - Just use a mutex per info set (simplest, but slow if contention is high)
- Recommendation: start with per-thread buffers, merge every 1000 iterations. This is the approach most production solvers use.

**Convergence monitoring:**

- Track exploitability every N iterations (N = 10,000 or 100,000). Full best-response computation is expensive for large games but necessary to know if you're converging.
- For intermediate monitoring, track average regret (cheaper but less informative).
- Save checkpoints periodically (every million iterations or every hour). Solves can crash and losing days of compute is painful.

**Nash distance targets:**

Solve to concrete accuracy targets rather than just "decreasing exploitability." Nash distance is measured as a percentage of the pot — it represents the maximum EV an opponent could gain per hand by deviating from Nash.

| Street | Target Nash Distance | Notes |
|--------|---------------------|-------|
| River | < 0.10% of pot | Small subgame, should converge tightly |
| Turn | < 0.25% of pot | Moderate subgame |
| Flop | < 0.50% of pot | Large subgame, more iterations needed |
| Full game | < 0.30% of pot | Overall blueprint quality |

These targets are based on GTO Wizard's published accuracy numbers. Solving below these thresholds means the strategy is effectively unexploitable in practice. Track Nash distance during convergence monitoring and terminate the solve when the target is reached rather than running a fixed number of iterations.

**Two-pointer showdown evaluation:**

At river terminal nodes (showdown), the solver must compute the EV for each hand in both players' ranges. This is the hottest operation in CFR — there are far more terminal nodes than any other type, and every CFR iteration visits them all.

- **Naive approach:** Compare every hand in Player A's range against every hand in Player B's range. O(n×m) where n,m ≈ 1000 combos each = ~1M comparisons per terminal node. Far too slow.
- **Two-pointer approach:**
  1. Pre-sort both players' river hands by evaluated rank (using the hand evaluator)
  2. Walk two pointers through the sorted lists simultaneously
  3. Maintain a running sum of opponent hand weights already passed (which we beat)
  4. Each hand's equity is the running sum divided by total opponent weight
  5. Handle card removal (can't play the same cards) by subtracting blocked combos as you sweep

```
Sorted hands:      [Rank 1 (best)] ──────────> [Rank 7462 (worst)]

Player A pointer:  ──>  (for each hand, running sum = what B hands we beat)
Player B pointer:  ──>  (advance B pointer until B's rank > A's rank)

EV(hand_a) = running_sum_of_beaten_B_hands / total_B_weight × pot
```

- **Complexity:** O(n log n) for sorting (once per board), O(n+m) for the sweep. ~2000 operations vs ~1M.
- **Card removal during sweep:** When the current hand from Player A shares a card with a hand in Player B's range, that B-hand is impossible. Track this with a bitmask — subtract blocked combos from the running sum as you encounter them.
- **Reference:** Shark 2.0's `CFRHelper.cpp` and `BestResponse.cpp` implement this with `RiverCombo` structs sorted by rank.

This is not optional — without it, CFR iteration speed will be orders of magnitude below the >10 iterations/sec target.

**Best practices:**

- Profile early. Run the solver for 1000 iterations, identify the hottest functions, optimize those first.
- The three biggest performance bottlenecks are usually: (1) tree traversal cache misses, (2) regret array random access patterns, (3) showdown evaluation at terminal nodes (solved by two-pointer sweep above).
- Compile with -O3 -march=native. Enable link-time optimization.

**Deliverable:** Solver runs on a 100bb NLHE abstraction, converges to Nash distance targets (see above) over millions of iterations. Strategy files can be saved and loaded.

**Status:** [ ] Not started

---

### Phase 5: Subgame Solving (Weeks 19-22)

**Goal:** Refine the blueprint strategy in real time for specific game situations.

**Files:**
- `src/subgame/subgame_solver.h/.cpp`
- `src/subgame/action_translation.h/.cpp`

**Unsafe nested solving (implement first):**

- When a specific game state is reached, extract the relevant subgame (all continuations from this point).
- Seed leaf node values from the blueprint strategy.
- Assume the opponent plays according to the blueprint distribution over hands (Bayesian update based on observed actions).
- Run DCFR on just this subgame with finer bet sizes and/or more card buckets.
- "Unsafe" because if the opponent deviates from the assumed blueprint, the re-solved strategy could be worse. In practice this rarely matters.

**Safe subgame solving (implement later):**

- Create an augmented subgame where the opponent can choose to enter the subgame or take an alternative payoff (the "gift").
- This guarantees the re-solved strategy is no more exploitable than the blueprint.
- More complex to implement but provides theoretical safety.

**Action translation (pseudo-harmonic mapping):**

- When the opponent bets a size not in your abstraction, map it to nearby abstract sizes probabilistically.
- Given opponent bet x between abstract sizes A and B: probability of mapping to A = (x - A)^(-1) / ((x - A)^(-1) + (B - x)^(-1))
- This satisfies boundary constraints, monotonicity, and scale invariance.
- Without good action translation, opponents can exploit your abstraction by betting weird sizes.

**Tradeoff: Solve depth.** Solving from the river is fast (seconds). Solving from the turn is slower (minutes). Solving from the flop in real time is difficult at deep stacks. Start with river-only re-solving and expand backward.

**Future enhancement: Depth-limited solving with value estimation.**

GTO Wizard's "Fast Mode" uses a more advanced approach: instead of solving from the current decision point all the way to showdown, it solves only a short lookahead (a few actions deep) and uses **neural networks to estimate the remaining game value at the leaf nodes**. As play progresses, it re-solves from each newly reached decision point. This is the approach used by Pluribus (Noam Brown, 2019).

Benefits:
- Dramatically faster: GTO Wizard reports 0.22% accuracy in 6 seconds on 2 cores vs PioSolver's ~80 minutes on 16 cores for comparable accuracy
- Can handle unrestricted bet sizes (no action abstraction needed)
- Naturally handles deep stacks where full-depth solving is intractable

This requires training a neural network on millions of self-play hands to produce accurate value estimates — a significant ML infrastructure investment. Not planned for v1, but worth considering if the traditional subgame solving in this phase proves too slow for real-time use at deep stacks.

**Deliverable:** Given a game state and blueprint, produce a refined strategy. Measure exploitability improvement vs. blueprint-only play.

**Status:** [ ] Not started

---

### Phase 6: CLI, API, and Polish (Weeks 23-26)

**Goal:** Clean library API, CLI tools, documentation, final optimization.

**Files:**
- `include/poker_solver/solver.h`
- `src/cli/main.cpp`
- `tools/query_strategy.cpp`
- `tools/measure_exploitability.cpp`

**Library API (public header):**

```cpp
namespace poker_solver {
    // Configuration
    struct SolverConfig {
        int stack_depth_bb;
        int num_iterations;
        std::string abstraction_path;
        std::string output_path;
        // DCFR parameters
        float dcfr_alpha = 1.5f;
        float dcfr_beta = 0.0f;
        float dcfr_gamma = 2.0f;
    };

    // Solve from scratch
    void solve(const SolverConfig& config);

    // Resume from checkpoint
    void solve_resume(const SolverConfig& config, const std::string& checkpoint);

    // Query a solved strategy
    struct StrategyResult {
        std::vector<std::string> actions;
        std::vector<float> probabilities;
        float expected_value;
    };
    StrategyResult query(const std::string& strategy_path,
                         const std::string& hole_cards,
                         const std::string& board,
                         const std::string& action_history);

    // Real-time subgame solving
    StrategyResult solve_subgame(const std::string& blueprint_path,
                                 const std::string& hole_cards,
                                 const std::string& board,
                                 const std::string& action_history,
                                 int num_iterations);
}
```

**CLI commands:**

```bash
# Build card abstraction
poker-solver abstraction --config abstraction.json --output data/buckets/

# Run solver
poker-solver solve --config solver.json --output data/strategy/ --threads 16

# Resume from checkpoint
poker-solver solve --resume data/strategy/checkpoint_1M.dat --threads 16

# Query strategy
poker-solver query --strategy data/strategy/ --hand "AhKs" --board "Td9c2h" --history "rc"

# Measure exploitability
poker-solver exploit --strategy data/strategy/ --samples 1000000

# Subgame solve
poker-solver subgame --blueprint data/strategy/ --hand "AhKs" --board "Td9c2h7d" --history "rcc" --iterations 10000
```

**Deliverable:** Working CLI that exposes all solver functionality. Clean API that could be embedded in other applications.

**Status:** [x] Complete

---

### Phase 6b: Multithreaded Solver

**Goal:** Parallelize DCFR iterations across multiple CPU cores for near-linear speedup. Currently the solver is single-threaded — on a modern Mac (8-12 cores) this leaves 85-90% of compute on the table.

**Why it matters:** A 100bb solve at 1M iterations takes hours single-threaded. With 8 cores it drops to under an hour. This is the single biggest performance win available.

**Expected speedup:**

| Cores | Speedup | Efficiency | Example hardware |
|-------|---------|------------|------------------|
| 1 | 1x | 100% | Baseline |
| 4 | ~3.5x | ~88% | M1/M2 base |
| 8 | ~6-7x | ~80-85% | M2/M3 Pro |
| 10-12 | ~8-10x | ~80% | M3/M4 Pro/Max |
| 16 | ~12-14x | ~75-85% | High-end desktop |
| 64 | ~45-55x | ~70-85% | Cloud instance |

Efficiency drops slightly at high core counts due to memory bandwidth contention on shared regret arrays, but external-sampling DCFR is embarrassingly parallel — each iteration samples independent cards and traverses the tree independently.

**Implementation:**

```
┌─────────────────────────────────────────────┐
│              CFRSolver::run()                │
│                                             │
│  ┌─────────┐ ┌─────────┐     ┌─────────┐   │
│  │Thread 0 │ │Thread 1 │ ... │Thread N │   │
│  │ RNG     │ │ RNG     │     │ RNG     │   │
│  │ cards   │ │ cards   │     │ cards   │   │
│  │ traverse│ │ traverse│     │ traverse│   │
│  │ local   │ │ local   │     │ local   │   │
│  │ regrets │ │ regrets │     │ regrets │   │
│  └────┬────┘ └────┬────┘     └────┬────┘   │
│       └───────────┼───────────────┘         │
│                   ▼                         │
│         Merge into shared store             │
│         (every ~1000 iterations)            │
└─────────────────────────────────────────────┘
```

**Key design:**

1. **Per-thread state:** Each thread gets its own RNG, sampled cards, and local regret/strategy delta buffers. No locking during traversal.
2. **Periodic merge:** Every ~1000 iterations, threads pause and merge their local deltas into the shared RegretStore. This is a brief synchronized step.
3. **Alternative: atomic updates:** Use `std::atomic<float>` with relaxed memory ordering for direct writes to the shared store. Simpler, slightly less cache-friendly, but eliminates the merge step. Shark 2.0 and OpenSpiel use this approach successfully.
4. **Thread pool:** `std::thread` or C++17 parallel algorithms. No external dependency needed.

**Files:**
- `src/solver/cfr_solver.h/.cpp` — add `run(iterations, num_threads)` overload
- `src/solver/thread_pool.h` — lightweight thread pool (optional, can use raw `std::thread`)

**Risks:**
- **Memory bandwidth:** At 16+ cores, the shared regret arrays may become a bottleneck. Profile with `perf` or Instruments.
- **Non-determinism:** Different thread scheduling = different iteration order = slightly different final strategy. Results are still correct (converge to same equilibrium) but not bit-for-bit reproducible across runs.

**Deliverable:** `poker-solver solve --threads N` uses N cores. Default to `std::thread::hardware_concurrency()`.

**Status:** [ ] Not started

---

### Phase 7: Archetype-Based Exploitation (Post-Release)

**Goal:** Classify opponents into behavioral archetypes based on observed tendencies, then apply pre-solved exploitative strategies for each archetype. Requires minimal data (100-500 hands) and avoids overfitting by discretizing tendencies into coarse buckets rather than estimating exact frequencies.

**Core Insight:** "Does this player bluff too much or too little?" is answerable in far fewer hands than "what is their exact bluff frequency on river check-raises?" By bucketing opponents into archetypes, we only need enough data to classify — not to estimate precisely.

**Architecture:**

```
[Hand History] ──> [Stat Tracker] ──> [Tendency Scores]
                                           │
                                    ┌──────┴──────┐
                                    ▼              ▼
                              [Classifier]    [Confidence]
                                    │              │
                                    ▼              ▼
                         [Archetype ID] ──> [Blend Weight α]
                                    │
                                    ▼
                    [Pre-solved Response Table]
                         arch_0: Nash (default)
                         arch_1: vs tight-passive
                         arch_2: vs loose-aggressive
                         ...
                                    │
                                    ▼
                        [Blended Strategy]
              σ = (1-α)·σ_nash + α·σ_archetype
```

**Classification Dimensions:**

| Dimension | What it measures | How to observe |
|-----------|-----------------|----------------|
| **Tightness** (tight ↔ loose) | How many hands they play | VPIP — visible from any hand |
| **Aggression** (passive ↔ aggressive) | Bet/raise frequency vs check/call frequency | PFR, AF ratio — visible from any hand |
| **Bluff frequency** (underbluffs ↔ overbluffs) | Ratio of bluffs to value bets | Requires showdown data — slower to estimate |
| **Fold-to-pressure** (calling station ↔ folder) | How often they fold to bets/raises | Fold-to-cbet, fold-to-3bet — visible when we bet |

Start with **2 dimensions** (tight/loose × passive/aggressive) for a 3×3 grid of 9 archetypes. Add bluff frequency as a third axis when showdown data accumulates.

**Archetypes (initial 2D grid):**

| | Passive | Neutral | Aggressive |
|---|---------|---------|------------|
| **Tight** | Nit | TAG | Aggro-nit |
| **Neutral** | Passive reg | Nash-like | Aggressive reg |
| **Loose** | Calling station | Loose-passive fish | Maniac |

**Modules:**

| Module | Description |
|--------|-------------|
| `exploit/hand_history` | Parse and store observed hands + actions from hand history files |
| `exploit/stat_tracker` | Compute running stats (VPIP, PFR, AF, fold-to-cbet, etc.) from observed hands |
| `exploit/classifier` | Map tendency scores to archetype ID + confidence level |
| `exploit/archetype_solver` | Offline: solve best-response strategy against each archetype's synthetic strategy |
| `exploit/strategy_blend` | Blend Nash and archetype response: σ = (1-α)·σ_nash + α·σ_archetype, where α scales with classification confidence |

**Key Design Decisions:**

- **Pre-solved responses:** Best-response strategies for each archetype are computed offline against synthetic opponent strategies that represent each archetype. At runtime it's just classify + lookup + blend. No solving needed in real-time.
- **Graceful degradation:** With very few hands, confidence is low, α is near 0, and we play Nash. As data accumulates, we classify more confidently and blend more toward the archetype response.
- **Coarse buckets prevent overfitting:** We only need to answer "which quadrant?" not "what exact frequency?" — this is robust even with 100-500 hands of data.
- **Recency weighting:** Decay older observations so classification adapts if opponent adjusts.

**Compute Estimates:**

| Task | Time | Memory | When |
|------|------|--------|------|
| Pre-solve 9 archetype responses | 9 × single tree traversal, minutes total | Same as blueprint query × 9 | Offline, once |
| Stat tracking (n hands) | O(n), milliseconds | Negligible | Per session |
| Classification | O(1), microseconds | Negligible | Per query |
| Strategy lookup + blend | O(1), microseconds | One strategy table per archetype in memory | Per query |

**Deliverable:** Pre-solved archetype response table + CLI commands to classify and query.

```bash
# Import hand histories
poker-solver import-hands --file villain_hands.csv --player "Villain"

# Classify opponent
poker-solver classify --player "Villain"
# Output: Archetype: loose-aggressive (confidence: 0.82)
#         VPIP: 38% | PFR: 29% | AF: 3.1 | Fold-to-cbet: 35%

# Query exploitative strategy for a spot
poker-solver query --player "Villain" --hand "AhKs" --board "Td9c2h" --history "rc"
```

**Status:** [ ] Not started

---

### Phase 8: Full Opponent Modeling (20K+ Hands)

**Goal:** When a large dataset of opponent hands is available (20K+), build a precise per-node opponent model and compute a tailored best-response strategy. This goes beyond archetype classification to exploit the specific nuances of an individual player's strategy.

**Prerequisites:** Phase 7 complete. This phase reuses `exploit/hand_history` and `exploit/strategy_blend` from Phase 7 but adds fine-grained modeling.

**When to Use Phase 8 vs Phase 7:**

| | Phase 7: Archetypes | Phase 8: Full Model |
|---|---------------------|---------------------|
| **Data needed** | 100-500 hands | 20,000+ hands |
| **Overfitting risk** | Low (coarse buckets) | Moderate (mitigated by Bayesian priors) |
| **Exploit precision** | Coarse — exploits general tendencies | Fine — exploits specific spot-by-spot leaks |
| **Compute at query time** | Lookup (microseconds) | Best-response tree traversal (seconds) |
| **Use case** | Live play, new opponents | Long-term rivals, study/analysis |

**Architecture:**

```
[Hand History DB]  ──>  [Opponent Modeler]  ──>  [Opponent Strategy σ_opp]
        (20K+ hands)          │
                              ├── Frequency counting per abstract bucket
                              ├── Bayesian prior (initialize from Nash blueprint)
                              └── Confidence estimation per decision point
                                        │
                                        ▼
[Nash Blueprint] ──>  [Best-Response Solver]  ──>  [Blended Strategy]
                              │
                              ├── Best-response tree traversal (single pass DP)
                              ├── Confidence-weighted blending with Nash
                              └── Exploitability bound check
```

**Modules:**

| Module | Description |
|--------|-------------|
| `exploit/opponent_model` | Estimate full opponent strategy σ_opp from observations using Bayesian updating with Nash prior. Per-node frequency estimates with confidence intervals. |
| `exploit/best_response` | Compute best-response strategy against fixed σ_opp via single-pass dynamic programming over the game tree |
| `exploit/confidence` | Per-node confidence scoring: nodes with <N observations fall back to archetype or Nash |

**Key Design Decisions:**

- **Bayesian priors from Nash:** Initialize opponent model at Nash equilibrium, update with observations. With 20K hands, most common decision points will have enough data to move significantly from the prior.
- **Hierarchical fallback:** For decision points with insufficient data, fall back to Phase 7 archetype classification rather than Nash. This gets the best of both phases.
- **Bounded exploitation:** Never go full best-response. σ = (1-α)·σ_nash + α·σ_br, where α scales with per-node confidence.
- **Recency weighting:** Exponential decay on older hands so the model reflects current tendencies.

**Pitfalls:**

1. **Data sparsity on later streets** — Even with 20K hands, river spots in specific lines may have <10 observations. Hierarchical fallback to archetype handles this.
2. **Opponent adjustment** — Long-term rivals adjust. Recency weighting and periodic model refresh are essential.
3. **Exploitability tradeoff** — Must monitor our own exploitability. If our blended strategy is >2x Nash exploitability, reduce α.

**Compute Estimates:**

| Task | Time | Memory |
|------|------|--------|
| Opponent model construction (20K hands) | O(n), seconds | ~50-100 MB for full node frequency table |
| Best-response computation | Single tree traversal, 1-10 seconds | Same as blueprint query |
| Bayesian range update per hand | O(1326 × actions), milliseconds | Negligible |
| Full model rebuild | Seconds | ~100 MB |

**Deliverable:** CLI commands for full opponent modeling and precise exploitation.

```bash
# Build full opponent model (requires 20K+ hands)
poker-solver model-player --player "Villain" --blueprint data/strategy/ --min-hands 20000

# Show per-spot deviations from Nash
poker-solver analyze-player --player "Villain" --board "Td9c2h" --history "rc"
# Output: Villain folds 62% here (Nash: 45%) — confidence: high (n=847)
#         Villain raises 8% here (Nash: 15%) — confidence: medium (n=847)
#         Recommended exploit: increase bluff frequency by 35%

# Query fully modeled exploitative strategy
poker-solver query --player "Villain" --mode full-model --hand "AhKs" --board "Td9c2h" --history "rc"
```

**Status:** [ ] Not started

---

### Nodelocking (Shared Feature — Phases 7 & 8)

**What it is:** The user manually locks one player's strategy at specific decision nodes in the game tree, and the solver computes the optimal counter-strategy against that locked behavior. This complements the automated archetype/modeling approach by letting users directly specify "what if villain always c-bets here?" or "what if villain never folds to 3-bets?"

**How it works:**
1. User selects a decision node in the game tree
2. User locks the opponent's strategy at that node — either by setting specific hand→action assignments or by adjusting action frequencies (e.g., "villain bets 80% here instead of Nash 55%")
3. Locked nodes stay frozen; all other nodes are re-solved to find the best response
4. The solver outputs the maximally exploitative counter-strategy

**Implementation:**
- Reuses the best-response computation from Phase 8 (`exploit/best_response`)
- Add a `nodelock` module that:
  - Accepts a list of (node_id, locked_strategy) pairs
  - Modifies the opponent's strategy at those nodes
  - Runs best-response computation with the modified strategy fixed
  - Reports EV gain from exploitation vs Nash

**Important caveat (from GTO Wizard's experience):** Exploits cascade unpredictably through the tree. Locking a villain's flop c-bet frequency might cause the optimal exploit to appear at a completely different decision point — e.g., changing our preflop 3-bet range rather than our flop response. This is mathematically correct but unintuitive. Users should be warned that the exploit may not appear "where they expect."

**CLI:**

```bash
# Lock villain's strategy at a specific node
poker-solver nodelock --blueprint data/strategy/ \
  --node "flop:IP:cbet" --board "Td9c2h" \
  --lock "bet=0.80,check=0.20" \
  --hand "AhKs"

# Output shows the adjusted strategy and EV gain
# EV gain vs Nash: +2.3 bb/100 in this spot
# Key adjustment: increase 3-bet frequency preflop with suited connectors
```

**Status:** [ ] Not started

---

### Phase 9: Web UI (PioSolver-Style Interface)

**Goal:** A browser-based GUI that provides a PioSolver-like experience — hand grid selector, tree navigator, strategy visualization, range viewer, and real-time subgame solving. The solver stays pure C++ behind a lightweight HTTP/WebSocket server.

**Architecture:**

```
┌─────────────────────────────────┐
│         React Frontend          │
│  ┌───────┐ ┌───────┐ ┌───────┐ │
│  │ Hand  │ │ Tree  │ │Range  │ │
│  │ Grid  │ │Browser│ │Viewer │ │
│  └───┬───┘ └───┬───┘ └───┬───┘ │
│      └─────────┼─────────┘     │
│                ▼               │
│         HTTP/WebSocket         │
└────────────────┬───────────────┘
                 │ JSON API
┌────────────────┴───────────────┐
│      C++ HTTP Server           │
│  (cpp-httplib or Crow)         │
│         │                      │
│    poker_solver API            │
│  solve / query / subgame /     │
│  exploit / tree-browse         │
└────────────────────────────────┘
```

**Backend (C++ JSON API server):**

| Endpoint | Method | Description |
|----------|--------|-------------|

| `/api/solve` | POST | Start a solve job (async, returns job ID) |
| `/api/solve/:id/status` | GET | Poll solve progress (iteration count, EV) |
| `/api/solve/:id/cancel` | POST | Cancel a running solve |
| `/api/query` | POST | Query strategy at a node for one hand |
| `/api/query/range` | POST | Query strategy for all 1326 combos at a node |
| `/api/subgame` | POST | Run subgame solve (async) |
| `/api/exploit` | POST | Measure exploitability (async) |
| `/api/tree` | GET | Get tree structure (node list with children) |
| `/api/tree/:node` | GET | Get node details (actions, pot, street) |
| `/ws/solve` | WebSocket | Real-time solve progress updates |

**Frontend (React):**

| Component | Description |
|-----------|-------------|
| Hand Grid | 13x13 grid of hand classes (colored by action frequency) |
| Strategy Bar | Colored bar showing fold/call/raise percentages per hand |
| Tree Browser | Click-through game tree navigator (like PioSolver's tree view) |
| Range Viewer | Full range display with EV and frequency for each combo |
| Solve Panel | Configure and launch solves with progress bar |
| Board Selector | Visual board card picker |

**Key Implementation Notes:**

- The C++ server wraps the existing `poker_solver` namespace API — no solver logic in the server layer
- Solves run in background threads; progress reported via WebSocket or polling
- Range queries batch all 1326 combos — need to add a batch query API to `solver_api.cpp`
- Tree browser needs a new `tree_export()` function to serialize the game tree as JSON
- Frontend served as static files from the C++ server (single binary deployment)

**Dependencies:**
- C++: `cpp-httplib` (header-only HTTP server) + `nlohmann/json` (JSON serialization)
- Frontend: React + Tailwind CSS + a charting library for equity graphs

**Deliverable:** A self-contained binary that serves a web UI on `localhost:8080`. User runs `poker-solver ui --port 8080` and opens a browser to interact with the solver visually.

**Status:** [ ] Not started

---

### Cross-Cutting Concerns for Phases 7 & 8

These considerations affect the design of both exploitation phases and should be addressed during implementation.

#### Hand History Format Support

Real hand histories come in wildly different formats per site (PokerStars, GGPoker, Winamax, Ignition, etc.). Each has its own text format with different conventions for actions, stakes, and player identification.

**Decision:** Define a standardized internal format and write per-site parsers. The `exploit/hand_history` module should:
1. Accept a clean internal format (CSV or JSON with defined schema) as the canonical input
2. Provide converter plugins for major sites (PokerStars format first — most widely used and documented)
3. Support PokerTracker/Hand2Note database exports as an alternative input path

#### Position-Dependent Modeling

A player who is a nit UTG might be a maniac on the button. Tendencies vary significantly by position. Averaging across positions destroys signal.

**Decision:** All stat tracking and archetype classification must be **segmented by position**, or at minimum split into IP (in position) vs OOP (out of position). This means:
- Phase 7: Archetype classification is per-position. A player could be classified as TAG when IP but passive-calling-station when OOP.
- Phase 8: Per-node frequency estimates naturally handle this since the game tree encodes position, but the confidence thresholds need to account for the data being split across positions (effectively dividing your sample size by ~2-6x).

#### Stack-Depth Segmentation

Players behave differently at 40bb vs 200bb. A hand history database spanning different stack depths will muddy the model.

**Decision:** Segment observations by effective stack depth buckets. Suggested initial buckets:
- Short stack: < 50bb
- Medium stack: 50-150bb
- Deep stack: > 150bb

Filter hand histories to the relevant stack depth range when building the model. If insufficient data exists for a specific depth, fall back to the nearest bucket or Nash.

#### Multi-Way Pot Handling

The solver is heads-up only, but real hand histories include multi-way pots. These hands still contain useful information about opponent tendencies.

**Decision:** Extract what we can from multi-way hands but flag the data as lower confidence:
- **Preflop stats** (VPIP, PFR, 3-bet frequency) are still valid from multi-way hands
- **Postflop tendencies** from multi-way pots are less reliable for heads-up modeling — a player may check a strong hand multi-way that they'd bet heads-up
- Tag multi-way observations with a lower confidence weight (e.g., 0.5x) when feeding into the model
- Allow CLI flag to include/exclude multi-way data: `--multiway include|exclude|downweight`

#### Bet Sizing Tells

Tracking only action type (fold/call/raise) misses a rich exploitation axis — **how much** a player bets/raises. Many players have sizing patterns correlated with hand strength.

**Decision:** Track bet sizing as a fraction of pot in addition to action type. Add sizing dimensions to the stat tracker:
- Small bet (< 40% pot)
- Medium bet (40-75% pot)
- Large bet (75-125% pot)
- Overbet (> 125% pot)

Phase 7 archetypes can include a "sizing tell" flag (e.g., "small bet = nutted, big bet = polarized"). Phase 8 can model sizing distributions per node.

#### Card Removal in Opponent Modeling (Phase 8)

When estimating what hands an opponent would take a specific line with, our own holding blocks certain combos. If we hold A♠K♠, the opponent can't have A♠A♣.

**Decision:** The `exploit/opponent_model` module must apply card removal (also called "blockers") when computing the opponent's range distribution at each node. This is critical for Phase 8 correctness. Steps:
1. Start with the full prior range (Nash frequencies or Bayesian posterior)
2. Zero out combos that conflict with our known cards and board cards
3. Renormalize the remaining range
4. Compute best response against the blocker-adjusted range

This is already standard in the core solver's equity calculations — reuse the same card removal logic.

#### Anonymous Player Tracking

On sites like Ignition/Bovada, players are anonymous — you cannot track the same player across sessions. This limits the data available for modeling.

**Decision:** Support two modes:
- **Identified mode:** Track a named player across sessions (PokerStars, GGPoker, etc.). Full Phase 7 and 8 capabilities.
- **Session-only mode:** For anonymous sites, build a model from the current session only. This limits you to Phase 7 archetype classification (unlikely to get 20K+ hands in one session). The classifier should be tuned to work well with smaller samples (100-500 hands) for this use case.

#### Solver Validation Against Known Solvers

For NLHE there is no closed-form Nash equilibrium to validate against. The only way to verify correctness is comparison against established solvers.

**Decision:** Add a validation step in Phase 4 (Full NLHE Solver):
- Pick 10-20 specific spots (varying board textures, stack depths, action sequences)
- Solve each spot with our solver and with PioSolver/GTO+
- Compare strategies — frequencies should be within a tolerance (e.g., ±5% for actions with >10% frequency)
- Document these comparison spots as regression tests

---

## Compute Estimates

These are rough estimates. Actual numbers depend heavily on abstraction granularity.

### At 100bb (baseline)

| Task | Time | Memory | Storage |
|------|------|--------|---------|
| Abstraction (500/1000/2000 buckets) | 1-2 days on 16 cores | 32-64 GB RAM | ~5 GB bucket files |
| Blueprint solve (1B iterations) | 3-7 days on 16 cores | 64-128 GB RAM | 10-30 GB strategy |
| River subgame solve | 1-10 seconds | < 1 GB | N/A |
| Turn subgame solve | 30-120 seconds | 2-8 GB | N/A |
| Exploitability (1M samples) | 1-4 hours | 16-32 GB | N/A |

### At 500bb (target)

| Task | Time | Memory | Storage |
|------|------|--------|---------|
| Abstraction | 2-5 days on 16 cores | 64-128 GB RAM | ~10 GB |
| Blueprint solve (1B iterations) | 1-3 weeks on 32 cores | 128-256 GB RAM | 30-100 GB |
| River subgame solve | 5-30 seconds | 1-4 GB | N/A |
| Turn subgame solve | 2-10 minutes | 4-16 GB | N/A |

The 500bb numbers are rough. If memory exceeds available RAM, the mmap approach will start paging to disk and performance will collapse. Profile at 200bb and 300bb before attempting 500bb.

---

## Performance Targets

| Component | Target | How to Measure |
|-----------|--------|----------------|
| Hand evaluation | < 10 ns per eval | bench_hand_eval |
| Equity calculation | > 1M evaluations/sec/core | bench_equity |
| CFR iteration speed | > 100K iterations/sec on Kuhn | bench_solver |
| CFR iteration speed | > 10 iterations/sec on NLHE 100bb | bench_solver |
| Memory per info set | < 32 bytes (regrets + strategy) | profiler |
| Tree build time | < 5 minutes for 100bb | bench_solver |

---

## Key References

### Must-Read Papers

1. Zinkevich et al. (2007) "Regret Minimization in Games with Incomplete Information" (original CFR)
2. Neller & Lanctot "An Introduction to Counterfactual Regret Minimization" (best tutorial, implement Kuhn from this)
3. Brown & Sandholm (2019) "Solving Imperfect-Information Games via Discounted Regret Minimization" (DCFR, our primary algorithm)
4. Brown & Sandholm (2017) "Safe and Nested Subgame Solving" (subgame solving techniques)
5. Sandholm (2015) CMU thesis "Computing Strong Game-Theoretic Strategies" (abstraction deep dive)
6. Brown & Sandholm (2018) "Superhuman AI for Heads-up No-Limit Poker: Libratus" (full system architecture)
7. Ganzfried & Sandholm (2013) "Action Translation in Extensive-Form Games" (pseudo-harmonic mapping)

### Open-Source Code to Study

- **TexasSolver** (C++, GUI): Production-quality GTO solver, closest to what we're building
- **postflop-solver** (Rust): Clean DCFR implementation, handles bunching effect
- **pycfr** (Python): Great for understanding CFR variants, not for performance
- **OMPEval** (C++): Hand evaluator we should likely integrate
- **DecisionHoldem** (C++): First open-source HUNL AI with subgame solving
- **ReBeL** (C++/Python): Facebook's RL+Search approach, Liar's Dice implementation open-sourced

### Useful Websites

- https://aipokertutorial.com (comprehensive CFR tutorial with code)
- https://stevengong.co/notes/Counterfactual-Regret-Minimization (notation reference)
- https://justinsermeno.com/posts/cfr/ (clean vanilla CFR walkthrough)
- https://blog.gtowizard.com/how-solvers-work/ (how commercial solvers work)

---

## Testing Strategy

Every module gets tested before the next one starts. No exceptions.

| Module | Test | Pass Criteria |
|--------|------|---------------|
| cards | Deck completeness, canonical hand mapping | All 52 cards unique, 169 canonical hands |
| hand_evaluator | Full 7-card enumeration | Rank distribution matches known frequencies |
| game_state | Random game playouts | No illegal states in 10M random games |
| CFR (Kuhn) | Nash equilibrium | Game value within 0.001 of -1/18 |
| CFR (Leduc) | Exploitability | Below 0.01 within 100K iterations |
| equity_calculator | Known equity matchups | AA vs KK equity within 0.5% of 81.95% |
| hand_clustering | Bucket sanity | AA, KK in same bucket; 72o nowhere near AA |
| game_tree | Tree size | Matches theoretical node count for simple configs |
| DCFR (NLHE) | Convergence | Exploitability decreasing over iterations |
| subgame solver | Improvement | Lower exploitability than blueprint alone |
| stat tracker | Known player types | Correctly computes VPIP, PFR, AF from synthetic hand histories |
| classifier | Archetype mapping | Classifies synthetic nit/TAG/LAG/maniac correctly with 200+ hands |
| archetype solver | Pre-solved responses | Each archetype response beats Nash EV against its target archetype |
| strategy blend | Bounded exploit | Exploitability of blended strategy < 2x Nash exploitability |
| opponent model (Phase 8) | Bayesian update | Posterior converges to true frequencies with 20K+ synthetic hands |
| best response (Phase 8) | Known exploits | Correctly max-bets vs opponent that always folds; always folds vs opponent that never bluffs |
| nodelock | Locked node exploit | EV of counter-strategy ≥ Nash EV against any locked opponent deviation |
| DCFR (NLHE) convergence | Nash distance | Reaches target Nash distance per street (river <0.10%, turn <0.25%, flop <0.50%) |

---

## Open Questions (Decide As We Go)

- **Board abstraction:** Should we also cluster board textures (e.g., treat monotone boards similarly)? This is an orthogonal compression axis. Defer until we see memory usage.
- **Warm starting:** Can we warm-start a finer solve from a coarser one? Brown & Sandholm (2016) showed this works. Implement if solve times get painful.
- **Pruning:** DCFR naturally prunes negative-regret actions. Additional pruning (skipping subtrees where regrets are very negative) can give 2-10x speedup. Add in Phase 4 optimization.
- **River solve shortcut:** River subgames are small enough to solve to near-zero exploitability. Consider always re-solving the river rather than using blueprint river play. This is what most commercial solvers do.
- **Rake adjustment:** The solver currently assumes zero-rake. Rake changes optimal strategy (tighter preflop, smaller pots less profitable). Defer to a future phase — get the zero-rake solver working first, then add rake-adjusted equilibria later.

---

### Phase 10: 6-Max Solver

**Goal:** Extend the solver from heads-up to 6-max (2-6 player) NLHE. This enables generating the positional range charts (UTG, HJ, CO, BTN, SB, BB) that commercial solvers like GTO Wizard provide.

**Why this is hard:** The game tree for 6 players is vastly larger than HU. Preflop alone, 6 players acting in sequence with raise/re-raise creates a tree orders of magnitude bigger. Pluribus (Brown 2019) solved 6-player poker but required heavy abstraction and clever techniques.

**C++ Changes Required:**

| Component | Current (HU) | Required (6-Max) | Impact |
|-----------|-------------|-------------------|--------|
| `GameState` | `hole[2]`, `stacks[2]`, `folded[2]`, etc. | Variable-size: `hole[N]`, `stacks[N]`, `folded[N]` where N=2-6 | **Major rewrite** — most state arrays are fixed at 2 |
| `GameState::actor` | Alternates 0/1 | Rotates through active (non-folded) players by position | Moderate — need position-aware turn order |
| `GameState::newHand()` | Posts SB/BB for 2 players | Posts SB/BB/antes, sets UTG as first actor preflop, BTN+1 postflop | Moderate |
| `GameTree` | Builds tree for 2 players | Builds tree for N players; each action node tracks which player acts | Moderate — tree builder generalizes, but tree SIZE explodes |
| `InfoSetManager` | `infoSetId(node, bucket)` for 2 players | Same concept, but N players × more nodes = much larger info set space | Sizing changes, logic similar |
| `RegretStore` | `regrets_[2]`, `strategy_[2]` | `regrets_[N]`, `strategy_[N]` — N arrays per type | Moderate — generalize from 2 to N |
| `CFRSolver::traverse()` | Traverser vs 1 opponent | Traverser vs N-1 opponents; sample all opponents' actions | **Core algorithm change** |
| `CFRSolver::terminalPayoff()` | 2-player showdown or fold | Multi-way showdown (split pots, side pots) or last-player-standing | **Significant** — side pots are complex |
| `CFRSolver::sampleCards()` | Deal 2×2 hole + 5 board = 9 cards | Deal N×2 hole + 5 board = 2N+5 cards | Minor |
| `HandEvaluator` | Compare 2 hands | Compare N hands, handle ties and split pots | Moderate |
| `ActionAbstraction` | Same sizes for both players | Position-dependent bet sizes (e.g., UTG opens smaller) | Minor — config change |
| `SolverConfig` | `stack_depth_bb` | Add `num_players`, `positions[]`, `ante` | Minor |
| CLI | `--stack N` | Add `--players N`, `--position POS` for queries | Minor |

**Approach: Solve preflop/flop separately from turn/river.**

The full 6-max tree from preflop through river is intractable to solve monolithically. The standard approach (used by Pluribus and commercial solvers):

1. **Preflop blueprint (6-way):** Solve the full 6-player preflop tree with very coarse postflop abstraction (roll out to showdown with equity estimates at leaf nodes). This produces the opening ranges (UTG range, CO 3-bet range, etc.).
2. **Postflop subgames (typically 2-3 way):** Once the hand reaches the flop, most players have folded. Solve the remaining 2-3 player subgame from flop onward with finer abstraction. Reuse the existing HU postflop solver for heads-up pots.
3. **Action abstraction:** Use fewer bet sizes in multi-way pots (check/bet/raise is usually enough) since multi-way play is less about bluffing and more about value.

**Memory estimates (6-max preflop only):**

| Component | Estimate |
|-----------|----------|
| Preflop info sets (6-player, 169 buckets) | ~50-100M info sets |
| Regret storage (6 players × info sets × actions) | ~20-50 GB |
| Tree structure | ~500 MB |

**Simplification: Heads-up postflop.** Most hands go to the flop 2-way or 3-way. For v1, once the hand is heads-up postflop, delegate to the existing HU solver. Multi-way postflop (3+ players seeing a flop) is much harder and can be deferred.

**Files:**
- `src/core/game_state.h/.cpp` — generalize to N players
- `src/tree/game_tree.h/.cpp` — N-player tree building
- `src/solver/cfr_solver.h/.cpp` — N-player traversal
- `src/solver/regret_store.h/.cpp` — N-player arrays
- `tests/test_6max.cpp` — verify preflop range convergence

**Deliverable:** `poker-solver solve --players 6 --stack 100` produces preflop ranges for all 6 positions. Postflop play delegates to HU/3-way subgame solvers.

**Status:** [ ] Not started

---

## CLI Solve Commands Reference

All commands assume the binary is built and available as `poker-solver`. Build with:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The binary is at `build/src/cli/poker-solver`.

---

### 1. Build Card Abstraction

Must be run once before solving. Produces bucket assignment files used by the solver.

```bash
# Standard abstraction (169/500/1000/2000 buckets)
poker-solver abstraction \
  --output data/buckets/standard \
  --preflop-buckets 169 \
  --flop-buckets 500 \
  --turn-buckets 1000 \
  --river-buckets 2000 \
  --threads 8

# Fine abstraction (for higher accuracy, more memory)
poker-solver abstraction \
  --output data/buckets/fine \
  --preflop-buckets 169 \
  --flop-buckets 1000 \
  --turn-buckets 2000 \
  --river-buckets 5000 \
  --threads 8

# Coarse abstraction (for testing or deep stacks)
poker-solver abstraction \
  --output data/buckets/coarse \
  --preflop-buckets 169 \
  --flop-buckets 200 \
  --turn-buckets 500 \
  --river-buckets 1000 \
  --threads 8
```

---

### 2. Heads-Up Solves

```bash
# ── Quick test solve (verify everything works) ──────────────────────────
poker-solver solve \
  --stack 100 \
  --iterations 10000 \
  --output data/strategy/hu_100bb_test \
  --threads 4 \
  --checkpoint 5000

# ── Standard 100bb HU solve ─────────────────────────────────────────────
poker-solver solve \
  --stack 100 \
  --iterations 1000000 \
  --output data/strategy/hu_100bb \
  --threads 8 \
  --checkpoint 100000

# ── High-iteration 100bb solve (production quality) ─────────────────────
poker-solver solve \
  --stack 100 \
  --iterations 10000000 \
  --output data/strategy/hu_100bb_10M \
  --threads 8 \
  --checkpoint 1000000

# ── Resume an interrupted solve ─────────────────────────────────────────
poker-solver solve \
  --stack 100 \
  --iterations 10000000 \
  --resume data/strategy/hu_100bb_10M \
  --threads 8

# ── 50bb solve (short stack) ────────────────────────────────────────────
poker-solver solve \
  --stack 50 \
  --iterations 1000000 \
  --output data/strategy/hu_50bb \
  --threads 8 \
  --checkpoint 100000

# ── 200bb solve (deep stack) ────────────────────────────────────────────
poker-solver solve \
  --stack 200 \
  --iterations 2000000 \
  --output data/strategy/hu_200bb \
  --threads 8 \
  --checkpoint 200000

# ── 500bb solve (very deep — needs coarse abstraction) ──────────────────
poker-solver solve \
  --stack 500 \
  --iterations 5000000 \
  --output data/strategy/hu_500bb \
  --threads 8 \
  --checkpoint 500000

# ── Custom DCFR parameters ──────────────────────────────────────────────
poker-solver solve \
  --stack 100 \
  --iterations 1000000 \
  --output data/strategy/hu_100bb_custom \
  --threads 8 \
  --alpha 1.5 --beta 0.5 --gamma 2.0 \
  --seed 12345
```

---

### 3. 6-Max Solves (Phase 10)

```bash
# ── 6-max preflop blueprint (100bb) ─────────────────────────────────────
poker-solver solve \
  --players 6 \
  --stack 100 \
  --iterations 5000000 \
  --output data/strategy/6max_100bb \
  --threads 8 \
  --checkpoint 500000

# ── 6-max preflop blueprint (50bb, tournament-depth) ────────────────────
poker-solver solve \
  --players 6 \
  --stack 50 \
  --iterations 3000000 \
  --output data/strategy/6max_50bb \
  --threads 8 \
  --checkpoint 300000

# ── 6-max preflop blueprint (200bb, deep) ───────────────────────────────
poker-solver solve \
  --players 6 \
  --stack 200 \
  --iterations 10000000 \
  --output data/strategy/6max_200bb \
  --threads 8 \
  --checkpoint 1000000
```

---

### 4. Measure Exploitability

Run after a solve to check convergence quality.

```bash
# ── Quick exploitability check ──────────────────────────────────────────
poker-solver exploit \
  --strategy data/strategy/hu_100bb \
  --samples 100000 \
  --stack 100

# ── Thorough exploitability measurement ─────────────────────────────────
poker-solver exploit \
  --strategy data/strategy/hu_100bb_10M \
  --samples 1000000 \
  --stack 100

# ── Deep stack exploitability ───────────────────────────────────────────
poker-solver exploit \
  --strategy data/strategy/hu_200bb \
  --samples 500000 \
  --stack 200
```

---

### 5. Query Strategy

Look up the solved GTO strategy for a specific spot.

```bash
# ── Preflop: what to do with AKs as SB (HU) ────────────────────────────
poker-solver query \
  --strategy data/strategy/hu_100bb \
  --hand "AhKh" \
  --stack 100

# ── Preflop: after a raise, should we 3-bet with QQ? ───────────────────
poker-solver query \
  --strategy data/strategy/hu_100bb \
  --hand "QdQc" \
  --history "r" \
  --stack 100

# ── Flop: c-bet decision on a dry board ─────────────────────────────────
poker-solver query \
  --strategy data/strategy/hu_100bb \
  --hand "AhKs" \
  --board "Td9c2h" \
  --history "rc" \
  --stack 100

# ── Turn: facing a bet after checking flop ──────────────────────────────
poker-solver query \
  --strategy data/strategy/hu_100bb \
  --hand "JhTh" \
  --board "Td9c2h7d" \
  --history "rckb" \
  --stack 100

# ── River: final street decision ────────────────────────────────────────
poker-solver query \
  --strategy data/strategy/hu_100bb \
  --hand "AsAd" \
  --board "Td9c2h7d5s" \
  --history "rcbc" \
  --stack 100

# ── 6-max: UTG opening range query ─────────────────────────────────────
poker-solver query \
  --strategy data/strategy/6max_100bb \
  --hand "AhKs" \
  --position UTG \
  --stack 100

# ── 6-max: CO facing UTG open ──────────────────────────────────────────
poker-solver query \
  --strategy data/strategy/6max_100bb \
  --hand "TsTc" \
  --position CO \
  --history "r" \
  --stack 100

# ── 6-max: BTN 3-bet spot ──────────────────────────────────────────────
poker-solver query \
  --strategy data/strategy/6max_100bb \
  --hand "AhQs" \
  --position BTN \
  --history "rr" \
  --stack 100

# ── 6-max: SB vs BB after everyone folds ────────────────────────────────
poker-solver query \
  --strategy data/strategy/6max_100bb \
  --hand "Kh9s" \
  --position SB \
  --history "fffff" \
  --stack 100
```

---

### 6. Subgame Solving

Re-solve a specific spot with finer granularity than the blueprint.

```bash
# ── River subgame re-solve (fast, ~seconds) ─────────────────────────────
poker-solver subgame \
  --hand "AhKs" \
  --board "Td9c2h7d5s" \
  --history "rcbc" \
  --iterations 10000 \
  --stack 100

# ── Turn subgame re-solve (slower, ~minutes) ────────────────────────────
poker-solver subgame \
  --hand "JhTh" \
  --board "Td9c2h7d" \
  --history "rckb" \
  --iterations 50000 \
  --stack 100

# ── Flop subgame re-solve (slowest, may take several minutes) ───────────
poker-solver subgame \
  --hand "AhKs" \
  --board "Td9c2h" \
  --history "rc" \
  --iterations 100000 \
  --stack 100
```

---

### 7. Exploitation Commands (Phase 7+)

```bash
# ── Import hand histories ───────────────────────────────────────────────
poker-solver import-hands \
  --file villain_hands.csv \
  --player "Villain" \
  --format pokerstars

# ── Classify opponent archetype ─────────────────────────────────────────
poker-solver classify \
  --player "Villain"

# ── Query exploitative strategy against a classified opponent ───────────
poker-solver query \
  --player "Villain" \
  --hand "AhKs" \
  --board "Td9c2h" \
  --history "rc" \
  --stack 100

# ── Build full opponent model (20K+ hands) ──────────────────────────────
poker-solver model-player \
  --player "Villain" \
  --blueprint data/strategy/hu_100bb \
  --min-hands 20000

# ── Analyze specific opponent leaks ─────────────────────────────────────
poker-solver analyze-player \
  --player "Villain" \
  --board "Td9c2h" \
  --history "rc"

# ── Nodelocking ─────────────────────────────────────────────────────────
poker-solver nodelock \
  --blueprint data/strategy/hu_100bb \
  --node "flop:IP:cbet" \
  --board "Td9c2h" \
  --lock "bet=0.80,check=0.20" \
  --hand "AhKs"
```

---

### 8. Web UI

```bash
# ── Launch the browser-based interface ──────────────────────────────────
poker-solver ui --port 8080
# Then open http://localhost:8080 in your browser
```

---

### 9. Full Workflow Example (End-to-End)

```bash
# Step 1: Build abstraction
poker-solver abstraction \
  --output data/buckets/standard \
  --preflop-buckets 169 --flop-buckets 500 \
  --turn-buckets 1000 --river-buckets 2000 \
  --threads 8

# Step 2: Solve HU 100bb blueprint
poker-solver solve \
  --stack 100 \
  --iterations 10000000 \
  --output data/strategy/hu_100bb \
  --threads 8 \
  --checkpoint 1000000

# Step 3: Check convergence
poker-solver exploit \
  --strategy data/strategy/hu_100bb \
  --samples 1000000 \
  --stack 100

# Step 4: Query specific spots
poker-solver query \
  --strategy data/strategy/hu_100bb \
  --hand "AhKs" --board "Td9c2h" --history "rc" \
  --stack 100

# Step 5: Re-solve a river spot with finer detail
poker-solver subgame \
  --hand "AhKs" --board "Td9c2h7d5s" \
  --history "rcbc" --iterations 10000 \
  --stack 100

# Step 6: Solve 6-max preflop ranges
poker-solver solve \
  --players 6 \
  --stack 100 \
  --iterations 5000000 \
  --output data/strategy/6max_100bb \
  --threads 8 \
  --checkpoint 500000

# Step 7: Query 6-max positional ranges
poker-solver query \
  --strategy data/strategy/6max_100bb \
  --hand "AhKs" --position UTG --stack 100

poker-solver query \
  --strategy data/strategy/6max_100bb \
  --hand "AhKs" --position BTN --stack 100

# Step 8: Launch UI for visual exploration
poker-solver ui --port 8080
```

---

## Changelog

- 2026-03-17: Initial document created with all design decisions and phase plan.
- 2026-03-17: Added Phase 7 (archetype-based exploitation), Phase 8 (full opponent modeling at 20K+ hands), and cross-cutting concerns (hand history formats, position-dependent modeling, stack-depth segmentation, multi-way handling, bet sizing tells, card removal, anonymous tracking, solver validation).
- 2026-03-18: Added suit isomorphism (Phase 3, locked-in decision) and two-pointer showdown evaluation (Phase 4) based on analysis of Shark 2.0 solver. Removed isomorphism from open questions.
- 2026-03-18: Added Nash distance targets (Phase 4), depth-limited solving future enhancement (Phase 5), nodelocking (Phases 7/8), and 1,755 distinct flops reference (Phase 3) based on GTO Wizard analysis.
- 2026-03-18: Phase 6 (CLI & API) completed. Added Phase 9 (Web UI — PioSolver-style browser interface with C++ HTTP server backend and React frontend).
- 2026-03-18: Added Phase 6b (Multithreaded Solver) — near-linear speedup via per-thread RNG/regret buffers with periodic merge or atomic updates.
- 2026-03-18: Added Phase 10 (6-Max Solver) and full CLI Solve Commands Reference covering all phases end-to-end. Phase 6b multithreading implemented (per-thread buffers with periodic merge).