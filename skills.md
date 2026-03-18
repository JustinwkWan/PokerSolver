# Skills Reference — PokerSolver

Living technical reference for the HUNL GTO Poker Solver project.
Covers algorithms, data structures, encoding conventions, gotchas, and performance baselines.

---

## 1. Card Encoding

| Property | Value |
|----------|-------|
| Type | `uint8_t` in [0, 51] |
| Rank | `card / 4` (or `card >> 2`) — 0=2, 1=3, ..., 8=T, 9=J, 10=Q, 11=K, 12=A |
| Suit | `card % 4` (or `card & 3`) — 0=clubs, 1=diamonds, 2=hearts, 3=spades |
| Sentinel | `kNoCard = 255` |
| Make card | `makeCard(rank, suit) = (rank << 2) | suit` |

**Dead card mask:** `CardMask = uint64_t`, bit `i` set means card `i` is dead.
```cpp
CardMask cardBit(Card c) { return uint64_t(1) << c; }
```

---

## 2. Chip Convention

- **1 unit = 0.5 BB** — SB posts 1, BB posts 2
- 100bb stack = 200 chip units
- All pot/stack math uses these integer units (no floats)

---

## 3. Hand Evaluator

- **Engine:** OMPEval (lookup-table based)
- **Return:** `uint16_t` rank — higher = better, opaque values for comparison only
- **Card index conversion:** Our card `c` → OMPEval index `rankOf(c) + suitOf(c) * 13`
- **Init cost:** ~200 KB lookup tables, one-time
- **Target:** < 10 ns per 7-card evaluation

**Categories** (low to high): HighCard, OnePair, TwoPair, ThreeOfAKind, Straight, Flush, FullHouse, FourOfAKind, StraightFlush

---

## 4. Game State

**Key fields:**
```
stack_size          — chips per player at start
hole[player][0..1]  — hole cards
board[0..4]         — community cards (kNoCard if undealt)
pot                 — chips from PREVIOUS streets
street_bet[2]       — chips committed THIS street
stacks[2]          — remaining chips
actor              — current player (0 or 1)
button             — dealer/SB seat
```

**Heads-up position rules:**
- Preflop: button (SB) acts first
- Postflop: OOP (non-button) acts first

**Min-raise rule:** Next raise >= previous raise size, or all-in. Tracked via `last_raise_size`.

**Street advances** when: both players have made >= 1 decision AND bets are matched (or someone folds/is all-in). Street bets collected into pot, `street_bet` reset to [0,0].

---

## 5. Abstraction Pipeline

### Dependency Chain (built backwards)
```
River centroids ──> Turn histograms ──> (Turn centroids) ──> Flop histograms
```

### Target Bucket Counts
| Street | Buckets | Method |
|--------|---------|--------|
| Preflop | 169 | Canonical lookup (no clustering) |
| Flop | 500 | MC runout sampling → histogram over river buckets → EMD clustering |
| Turn | 1000 | Enumerate rivers → histogram over river buckets → EMD clustering |
| River | 2000 | Average equity across sampled boards → L2 clustering |

### Hand Indexing
- **Triangular mapping** for C(52,2) = 1326 unique pairs
- `handIndex(c1, c2)`: canonical with `c1 < c2`, formula: `c1 * (2*52 - c1 - 3) / 2 + c2 - 1`
- Symmetric: `handIndex(a, b) == handIndex(b, a)`

### River Buckets (`buildRiverBuckets`)
1. Sample N random 5-card boards
2. For each board, `allRiverEquities()` → equity for all C(47,2)=1081 valid hole combos
3. Average equity per hand across boards
4. k-means L2 on 1326 averaged equities → bucket assignments
5. Return sorted centroids (needed by turn pipeline)

### Turn Buckets (`buildTurnBuckets`)
1. Sample N random 4-card boards
2. For each board + each valid hole combo, enumerate all 46-48 river cards
3. For each 5-card board: exact equity → assign to nearest river centroid
4. Build histogram over river buckets, normalize to sum=1.0
5. Average histograms per hand across boards, re-normalize
6. k-means EMD on 1326 histograms → turn bucket assignments

### Flop Buckets (`buildFlopBuckets`)
1. Sample N random 3-card flops
2. For each flop + valid hole combo, Monte Carlo sample M turn+river runouts
3. Fisher-Yates partial shuffle for conflict-free sampling
4. Each runout: exact equity → assign to river bucket
5. Build histogram over river buckets, normalize
6. Average across flops, re-normalize
7. k-means EMD → flop bucket assignments

### Preflop Buckets (`buildPreflopBuckets`)
- 169 canonical classes via `canonicalPreflopHand(c1, c2)` lookup table
- Layout: 0–12 pairs (AA=0..22=12), 13–90 suited, 91–168 offsuit
- Must call `initCards()` before use

---

## 6. Clustering Algorithms

### k-means++ Initialization
- First centroid: random data point
- Subsequent: probability proportional to D(x)^2 (squared distance to nearest existing centroid)
- Ensures well-spread initial centroids

### k-means L2 (river)
- Standard Lloyd's algorithm on 1D equity values
- Convergence: max centroid shift < tolerance (1e-6)
- Used for river because equity is a scalar

### k-means EMD (turn/flop)
- Distance: 1D Earth Mover's Distance (exact, O(n) per pair)
- EMD formula: scan cumulative difference of two histograms, sum absolute values
- Centroids: arithmetic mean of member histograms (approximation — true Wasserstein barycenter is expensive)
- Convergence: max centroid EMD shift < tolerance
- Centroids stored flat: `centroids[k * dim + d]`

### EMD 1D
```cpp
double emd1d(const double* a, const double* b, int n) {
    double cum = 0, total = 0;
    for (int i = 0; i < n; ++i) { cum += a[i] - b[i]; total += abs(cum); }
    return total;
}
```

---

## 7. Equity Calculation

### River (Exact)
- `riverEquityExact(hole, board)`: enumerate C(45,2) = 990 opponent combos
- Win = 1.0, Tie = 0.5, Loss = 0.0; average over all opponents
- Assert: dead card popcount == 7 (2 hole + 5 board)

### Batch River
- `allRiverEquities(board)`: compute equity for ALL C(47,2) = 1081 valid hole combos
- Used by `buildRiverBuckets` for efficiency

### Monte Carlo (Flop/Turn)
- `equityMonteCarlo(hole, board, num_board, samples, rng)`
- Samples: 2 opponent cards + (5 - num_board) random board completions
- Fisher-Yates partial shuffle on remaining deck
- Deterministic given same seed

---

## 8. CFR (Counterfactual Regret Minimization)

### Toy Game Implementation (Phase 2)
- **Storage:** `unordered_map<string, InfoSet>` (hash-map, suitable for small games)
- **Key format:** player card + action history, e.g. `"K:cb"`, `"Q:1:rrc"`
- **Regret matching:** `strategy[a] = max(0, cumulative_regret[a]) / sum`; uniform if all <= 0
- **Average strategy:** `cumulative_strategy[a] / sum` — this is the Nash approximation

### DCFR Parameters (Phase 4)
- alpha = 1.5 (positive regret discount)
- beta = 0.0 (negative regret discount)
- gamma = 2.0 (strategy sum discount)
- External sampling variant

### Verified Results
| Game | Expected Value | Achieved | Iterations |
|------|---------------|----------|------------|
| Kuhn poker | -1/18 = -0.0556 | within 0.001 | 100K |
| Leduc poker | -0.0856 | within 0.01 | 200K |

---

## 9. Gotchas & Lessons Learned

### Chance Node Regret Weighting (Leduc)
**Bug:** Iterating over card *ranks* at chance nodes instead of card *copies*.
**Fix:** Iterate over individual card copies so regret updates are naturally weighted by each community card's probability. A rank with 2 remaining copies contributes twice.

### Same-Rank Deals (Leduc)
**Bug:** Forgetting that both players can hold the same rank (different suits) in 6-card Leduc.
**Fix:** Include same-rank deals in training with combinatorial weight 2 (vs 4 for different-rank). Best response opponent probability: 1/5 for same-rank, 2/5 for different-rank.

### Histogram Normalization
**Rule:** Always re-normalize after averaging histograms across boards. Averaging already-normalized histograms preserves the sum, but floating point drift can accumulate. Defensive re-normalization costs nothing.

### Dead Card Masking
**Rule:** Use `CardMask` (uint64_t bitmask) for O(1) conflict detection. Assert dead card count at boundaries (river should always have exactly 7 dead cards).

### Unvisited Hands
**Rule:** Hands that never appear in sampled boards get a uniform histogram (1/dim per bin). This prevents NaN from zero-division and gives a neutral prior.

### EMD Arithmetic Mean Approximation
**Fact:** Using arithmetic mean of histograms as the centroid in EMD k-means is an approximation. The true Wasserstein barycenter requires an LP solve per iteration. The approximation is standard in poker abstraction literature and works well in practice.

### Fisher-Yates for Sampling
**Rule:** Use partial Fisher-Yates shuffle (swap first K elements) to sample K cards without replacement. More efficient than rejection sampling and guaranteed conflict-free.

---

## 10. Module Build Order

```
Phase 1 — Core Engine
  core/cards
  core/hand_evaluator (depends: cards)
  core/game_state (depends: cards)

Phase 2 — CFR on Toy Games
  solver/cfr (depends: core)
  Verify: Kuhn value = -1/18, Leduc value = -0.0856

Phase 3 — Abstraction Pipeline  [COMPLETE]
  abstraction/equity_calculator (depends: hand_evaluator, game_state)
  abstraction/hand_clustering (depends: equity_calculator)
  Build order: river → turn → flop → preflop

Phase 4 — Full NLHE Solver
  abstraction/action_abstraction (depends: game_state)
  tree/node (depends: game_state, action_abstraction)
  tree/info_set (depends: node, hand_clustering)
  tree/game_tree (depends: node, info_set)
  solver/regret_store (depends: info_set) — uses mmap
  solver/cfr_solver (depends: game_tree, regret_store)
  solver/exploitability (depends: cfr_solver)

Phase 5 — Subgame Solving
  subgame/action_translation (depends: game_tree)
  subgame/subgame_solver (depends: cfr_solver, action_translation)

Phase 6 — CLI & Polish
```

---

## 11. Performance Targets & Baselines

| Component | Target | Notes |
|-----------|--------|-------|
| Hand evaluation | < 10 ns/eval | OMPEval lookup tables |
| Equity calculation | > 1M evals/sec/core | 990 opponent combos per call |
| CFR on Kuhn | > 100K iter/sec | Hash-map info sets |
| CFR on NLHE 100bb | > 10 iter/sec | Flat array + mmap |
| Memory per info set | < 32 bytes | Float32 regrets |

### Production Compute Estimates (100bb)
| Task | Time | Memory | Storage |
|------|------|--------|---------|
| Abstraction (500/1000/2000) | 1–2 days, 16 cores | 32–64 GB | ~5 GB |
| Blueprint solve (1B iter) | 3–7 days, 16 cores | 64–128 GB | 10–30 GB |
| River subgame solve | 1–10 sec | < 1 GB | N/A |
| Turn subgame solve | 30–120 sec | 2–8 GB | N/A |
| Exploitability (1M samples) | 1–4 hours | 16–32 GB | N/A |

---

## 12. Testing Strategy

**Correctness tests (run by Claude):**
- Unit tests per module (GTest), one binary per test file
- Verify algorithms on small/synthetic data
- Check determinism (same seed → same result)
- Validate ranges, counts, uniqueness
- Cross-check batch vs individual computations

**Performance tests (run by user):**
- Full-scale abstraction pipeline with production bucket counts
- End-to-end buildRiverBuckets / buildTurnBuckets / buildFlopBuckets
- CFR convergence on full NLHE game tree
- Timing benchmarks for hand evaluation and equity calculation

**Test counts by phase:**
| Binary | Tests | Phase |
|--------|-------|-------|
| test_cards | 12 | 1 |
| test_hand_eval | 5 (1 skipped) | 1 |
| test_game_state | 10 | 1 |
| test_kuhn | 2 | 2 |
| test_leduc | 3 | 2 |
| test_equity | 10 | 3 |
| test_clustering | 25 | 3 |
| **Total** | **67** | |

---

## 13. Visualization Guide

| Phase | What You Can Visualize |
|-------|----------------------|
| Phase 3 (now) | Equity distributions per hand, bucket heatmaps (1326 hands colored by bucket), river centroid spread, EMD centroid histograms, clustering quality metrics (total cost, silhouette) |
| Phase 4 | Strategy heatmaps (raise/call/fold % by hand class and position), convergence curves (exploitability vs iterations), bet sizing distributions, action frequency by street |
| Phase 5 | Re-solving quality (blueprint vs refined strategy delta), action translation accuracy, subgame boundary payoff distributions |

**Recommended tools:** Python matplotlib/seaborn reading exported CSV/binary bucket files, or a small C++ CLI that dumps strategy tables to stdout.
