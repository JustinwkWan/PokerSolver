#include "solver/cfr_solver.h"

#include <gtest/gtest.h>
#include <cmath>

// ── Helpers ──────────────────────────────────────────────────────────────

// Simple bucket function: maps all hands to a small number of buckets
// based on preflop canonical hand index or equity-like heuristic.
static int simpleBucket(int /*player*/, int street,
                         const Card hole[2],
                         const Card /*board*/[5],
                         int /*num_board*/) {
    // For testing, use a tiny bucket count per street.
    // Preflop: canonical hand index mod 3 (3 buckets).
    // Postflop: simple rank-based hash mod small bucket count.
    initCards();
    if (street == 0) {
        return canonicalPreflopHand(hole[0], hole[1]) % 3;
    }
    // Postflop: combine hole card ranks into a bucket.
    int r0 = rankOf(hole[0]);
    int r1 = rankOf(hole[1]);
    int hash = r0 * 13 + r1;
    int buckets_per_street[] = {3, 5, 5, 10};
    return hash % buckets_per_street[street];
}

static constexpr std::array<int, 4> kTestBuckets = {3, 5, 5, 10};

static GameTree makeSmallTree() {
    BetSizeConfig cfg;
    cfg.preflop = {1.0};
    cfg.flop    = {1.0};
    cfg.turn    = {1.0};
    cfg.river   = {1.0};
    GameTree tree(20, ActionAbstraction(cfg));  // 10bb stack
    tree.build();
    return tree;
}

// ── Construction ─────────────────────────────────────────────────────────

TEST(CFRSolver, Constructs) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);
    EXPECT_EQ(solver.iteration(), 0u);
}

// ── Single iteration runs without crashing ───────────────────────────────

TEST(CFRSolver, SingleIterationRuns) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    solver.runIteration(0);
    EXPECT_EQ(solver.iteration(), 1u);
}

// ── Multiple iterations run ──────────────────────────────────────────────

TEST(CFRSolver, MultipleIterationsRun) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    solver.run(100);
    EXPECT_EQ(solver.iteration(), 100u);
}

// ── Regrets are non-zero after iterations ────────────────────────────────

TEST(CFRSolver, RegretsNonZero) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    solver.run(100);

    // At least some regret entries should be non-zero.
    bool found_nonzero = false;
    for (int p = 0; p < 2; ++p) {
        for (uint64_t i = 0; i < store.totalEntries(); ++i) {
            if (store.regrets(p)[i] != 0.0f) {
                found_nonzero = true;
                break;
            }
        }
        if (found_nonzero) break;
    }
    EXPECT_TRUE(found_nonzero) << "All regrets are zero after 100 iterations";
}

// ── Strategy sums are non-zero (opponent nodes visited) ──────────────────

TEST(CFRSolver, StrategySumsNonZero) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    solver.run(100);

    bool found_nonzero = false;
    for (int p = 0; p < 2; ++p) {
        for (uint64_t i = 0; i < store.totalEntries(); ++i) {
            if (store.strategy(p)[i] != 0.0f) {
                found_nonzero = true;
                break;
            }
        }
        if (found_nonzero) break;
    }
    EXPECT_TRUE(found_nonzero) << "All strategy sums are zero after 100 iterations";
}

// ── Average strategy is a valid probability distribution ─────────────────

TEST(CFRSolver, AverageStrategyValid) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    solver.run(500);

    // Check a few info sets have valid probability distributions.
    for (uint32_t base = 0; base < std::min(mgr.numBaseInfoSets(), 20u); ++base) {
        int player = mgr.playerForBase(base);
        int street = mgr.streetForBase(base);
        int num_actions = mgr.numActionsForBase(base);
        int bucket = 0;  // check bucket 0

        // Need a node index with this base to get info_set_id.
        // Find any node with info_set_idx == base.
        for (uint32_t ni = 0; ni < tree.numNodes(); ++ni) {
            const auto& n = tree.node(ni);
            if (n.type != NodeType::Action) continue;
            if (n.info_set_idx != base) continue;

            uint64_t info_id = mgr.infoSetId(ni, bucket);
            float avg[8];
            store.getAverageStrategy(player, info_id, num_actions, avg);

            float sum = 0.0f;
            for (int a = 0; a < num_actions; ++a) {
                EXPECT_GE(avg[a], 0.0f);
                EXPECT_LE(avg[a], 1.0f);
                sum += avg[a];
            }
            EXPECT_NEAR(sum, 1.0f, 1e-5f);
            break;  // only need one node per base
        }
    }
}

// ── Alternating traverser ────────────────────────────────────────────────

TEST(CFRSolver, AlternatesTraverser) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    // run(2) should run player 0 then player 1.
    solver.run(2);
    EXPECT_EQ(solver.iteration(), 2u);
}

// ── DCFR discounting actually discounts ──────────────────────────────────

TEST(CFRSolver, DiscountingReducesRegrets) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());

    // Manually set large regrets, then run one iteration to trigger discounting.
    for (uint64_t i = 0; i < store.totalEntries(); ++i) {
        store.regrets(0)[i] = 100.0f;
        store.regrets(1)[i] = 100.0f;
    }

    CFRSolver solver(tree, mgr, store, simpleBucket);
    solver.runIteration(0);

    // After discounting (iteration 1, alpha=1.5): weight = 1^1.5 / (1^1.5 + 1) = 0.5.
    // But the iteration also adds new regrets, so values shouldn't be exactly 50.
    // Check that at least some values are less than 100 (discounted).
    bool found_discounted = false;
    for (uint64_t i = 0; i < store.totalEntries(); ++i) {
        if (store.regrets(0)[i] < 100.0f && store.regrets(0)[i] != 0.0f) {
            found_discounted = true;
            break;
        }
    }
    EXPECT_TRUE(found_discounted) << "No regrets were discounted";
}

// ── Deterministic with same seed ─────────────────────────────────────────

TEST(CFRSolver, DeterministicWithSameSeed) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);

    RegretStore store1(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver1(tree, mgr, store1, simpleBucket, {}, 123);
    solver1.run(50);

    RegretStore store2(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver2(tree, mgr, store2, simpleBucket, {}, 123);
    solver2.run(50);

    // Both should produce identical results.
    for (int p = 0; p < 2; ++p) {
        for (uint64_t i = 0; i < store1.totalEntries(); ++i) {
            EXPECT_FLOAT_EQ(store1.regrets(p)[i], store2.regrets(p)[i]);
            EXPECT_FLOAT_EQ(store1.strategy(p)[i], store2.strategy(p)[i]);
        }
    }
}

// ── Different seeds produce different results ────────────────────────────

TEST(CFRSolver, DifferentSeedsDiffer) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);

    RegretStore store1(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver1(tree, mgr, store1, simpleBucket, {}, 42);
    solver1.run(50);

    RegretStore store2(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver2(tree, mgr, store2, simpleBucket, {}, 999);
    solver2.run(50);

    // At least some values should differ.
    bool found_diff = false;
    for (uint64_t i = 0; i < store1.totalEntries(); ++i) {
        if (store1.regrets(0)[i] != store2.regrets(0)[i]) {
            found_diff = true;
            break;
        }
    }
    EXPECT_TRUE(found_diff);
}

// ── Larger tree (default config, 100bb) ──────────────────────────────────

TEST(CFRSolver, LargerTreeRuns) {
    ActionAbstraction aa;  // default: {0.5, 1.0} + all-in
    GameTree tree(200, aa);  // 100bb
    tree.build();

    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    // Just verify it doesn't crash on a larger tree.
    solver.run(10);
    EXPECT_EQ(solver.iteration(), 10u);
}

// ── Parallel: runs without crashing ─────────────────────────────────────

TEST(CFRSolver, ParallelRuns) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    solver.runParallel(200, 4, 50);
    EXPECT_EQ(solver.iteration(), 200u);
}

// ── Parallel: produces non-zero regrets and strategy sums ───────────────

TEST(CFRSolver, ParallelProducesNonZeroValues) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    solver.runParallel(500, 4);

    bool found_regret = false;
    bool found_strat = false;
    for (int p = 0; p < 2; ++p) {
        for (uint64_t i = 0; i < store.totalEntries(); ++i) {
            if (store.regrets(p)[i] != 0.0f) found_regret = true;
            if (store.strategy(p)[i] != 0.0f) found_strat = true;
            if (found_regret && found_strat) break;
        }
        if (found_regret && found_strat) break;
    }
    EXPECT_TRUE(found_regret) << "All regrets are zero after parallel run";
    EXPECT_TRUE(found_strat) << "All strategy sums are zero after parallel run";
}

// ── Parallel: average strategy is valid probability distribution ────────

TEST(CFRSolver, ParallelAverageStrategyValid) {
    auto tree = makeSmallTree();
    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    solver.runParallel(1000, 4);

    for (uint32_t base = 0; base < std::min(mgr.numBaseInfoSets(), 20u); ++base) {
        int player = mgr.playerForBase(base);
        int num_actions = mgr.numActionsForBase(base);
        int bucket = 0;

        for (uint32_t ni = 0; ni < tree.numNodes(); ++ni) {
            const auto& n = tree.node(ni);
            if (n.type != NodeType::Action) continue;
            if (n.info_set_idx != base) continue;

            uint64_t info_id = mgr.infoSetId(ni, bucket);
            float avg[8];
            store.getAverageStrategy(player, info_id, num_actions, avg);

            float sum = 0.0f;
            for (int a = 0; a < num_actions; ++a) {
                EXPECT_GE(avg[a], 0.0f);
                EXPECT_LE(avg[a], 1.0f);
                sum += avg[a];
            }
            EXPECT_NEAR(sum, 1.0f, 1e-5f);
            break;
        }
    }
}

// ── Parallel on larger tree ─────────────────────────────────────────────

TEST(CFRSolver, ParallelLargerTree) {
    ActionAbstraction aa;
    GameTree tree(200, aa);
    tree.build();

    InfoSetManager mgr(tree, kTestBuckets);
    RegretStore store(mgr.totalInfoSets(), mgr.maxActions());
    CFRSolver solver(tree, mgr, store, simpleBucket);

    solver.runParallel(100, 4);
    EXPECT_EQ(solver.iteration(), 100u);
}
