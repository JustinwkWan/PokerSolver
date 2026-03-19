#include "subgame/subgame_solver.h"

#include <gtest/gtest.h>

// ── Helpers ──────────────────────────────────────────────────────────────

static int simpleBucket(int /*player*/, int street,
                         const Card hole[2],
                         const Card /*board*/[5],
                         int /*num_board*/) {
    initCards();
    if (street == 0)
        return canonicalPreflopHand(hole[0], hole[1]) % 3;
    int r0 = rankOf(hole[0]);
    int r1 = rankOf(hole[1]);
    int hash = r0 * 13 + r1;
    int buckets[] = {3, 5, 5, 10};
    return hash % buckets[street];
}

static constexpr std::array<int, 4> kTestBuckets = {3, 5, 5, 10};

// ── Subgame solve from hand start ────────────────────────────────────────

TEST(SubgameSolver, SolvesFromStart) {
    initCards();
    GameState state = GameState::newHand(20, 0);
    state = state.withHoleCards(
        cardFromStr("Ah"), cardFromStr("Kh"),
        cardFromStr("Tc"), cardFromStr("9c"));

    BetSizeConfig cfg;
    cfg.preflop = {1.0};
    cfg.flop    = {1.0};
    cfg.turn    = {1.0};
    cfg.river   = {1.0};
    ActionAbstraction aa(cfg);

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {cardFromStr("Ah"), cardFromStr("Kh")};
    hole[1] = {cardFromStr("Tc"), cardFromStr("9c")};
    std::array<Card, 5> board = {
        cardFromStr("5d"), cardFromStr("2c"), cardFromStr("7s"),
        cardFromStr("Jd"), cardFromStr("3h")
    };

    SubgameConfig config;
    config.num_iterations = 100;
    config.seed = 42;

    auto result = SubgameSolver::solve(state, aa, kTestBuckets, simpleBucket,
                                        hole, board, config);

    EXPECT_GT(result.actions.size(), 0u);
    EXPECT_EQ(result.strategy.size(), result.actions.size());
    EXPECT_EQ(result.num_iterations, 100);
    EXPECT_GT(result.num_nodes, 0u);
    EXPECT_GT(result.num_info_sets, 0u);
}

// ── Strategy is a valid probability distribution ─────────────────────────

TEST(SubgameSolver, StrategyIsValid) {
    initCards();
    GameState state = GameState::newHand(20, 0);
    state = state.withHoleCards(
        cardFromStr("Ah"), cardFromStr("Kh"),
        cardFromStr("Tc"), cardFromStr("9c"));

    BetSizeConfig cfg;
    cfg.preflop = {1.0};
    cfg.flop    = {1.0};
    cfg.turn    = {1.0};
    cfg.river   = {1.0};

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {cardFromStr("Ah"), cardFromStr("Kh")};
    hole[1] = {cardFromStr("Tc"), cardFromStr("9c")};
    std::array<Card, 5> board = {
        cardFromStr("5d"), cardFromStr("2c"), cardFromStr("7s"),
        cardFromStr("Jd"), cardFromStr("3h")
    };

    SubgameConfig config;
    config.num_iterations = 200;

    auto result = SubgameSolver::solve(state, ActionAbstraction(cfg),
                                        kTestBuckets, simpleBucket,
                                        hole, board, config);

    float sum = 0.0f;
    for (float p : result.strategy) {
        EXPECT_GE(p, 0.0f);
        EXPECT_LE(p, 1.0f);
        sum += p;
    }
    EXPECT_NEAR(sum, 1.0f, 1e-5f);
}

// ── Finer abstraction produces more actions ──────────────────────────────

TEST(SubgameSolver, FinerAbstractionMoreActions) {
    initCards();
    GameState state = GameState::newHand(40, 0);  // 20bb
    state = state.withHoleCards(
        cardFromStr("Ah"), cardFromStr("Kh"),
        cardFromStr("Tc"), cardFromStr("9c"));

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {cardFromStr("Ah"), cardFromStr("Kh")};
    hole[1] = {cardFromStr("Tc"), cardFromStr("9c")};
    std::array<Card, 5> board = {
        cardFromStr("5d"), cardFromStr("2c"), cardFromStr("7s"),
        cardFromStr("Jd"), cardFromStr("3h")
    };

    SubgameConfig config;
    config.num_iterations = 50;

    // Coarse: 1 bet size.
    BetSizeConfig coarse;
    coarse.preflop = {1.0};
    coarse.flop = {1.0};
    coarse.turn = {1.0};
    coarse.river = {1.0};

    auto r_coarse = SubgameSolver::solve(state, ActionAbstraction(coarse),
                                          kTestBuckets, simpleBucket,
                                          hole, board, config);

    // Fine: 3 bet sizes.
    BetSizeConfig fine;
    fine.preflop = {0.33, 0.67, 1.0};
    fine.flop = {0.33, 0.67, 1.0};
    fine.turn = {0.33, 0.67, 1.0};
    fine.river = {0.33, 0.67, 1.0};

    auto r_fine = SubgameSolver::solve(state, ActionAbstraction(fine),
                                        kTestBuckets, simpleBucket,
                                        hole, board, config);

    // Finer abstraction should have more nodes.
    EXPECT_GT(r_fine.num_nodes, r_coarse.num_nodes);
}

// ── Deterministic with same seed ─────────────────────────────────────────

TEST(SubgameSolver, DeterministicSameSeed) {
    initCards();
    GameState state = GameState::newHand(20, 0);
    state = state.withHoleCards(
        cardFromStr("Ah"), cardFromStr("Kh"),
        cardFromStr("Tc"), cardFromStr("9c"));

    BetSizeConfig cfg;
    cfg.preflop = {1.0};
    cfg.flop = {1.0};
    cfg.turn = {1.0};
    cfg.river = {1.0};

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {cardFromStr("Ah"), cardFromStr("Kh")};
    hole[1] = {cardFromStr("Tc"), cardFromStr("9c")};
    std::array<Card, 5> board = {
        cardFromStr("5d"), cardFromStr("2c"), cardFromStr("7s"),
        cardFromStr("Jd"), cardFromStr("3h")
    };

    SubgameConfig config;
    config.num_iterations = 100;
    config.seed = 123;

    auto r1 = SubgameSolver::solve(state, ActionAbstraction(cfg),
                                    kTestBuckets, simpleBucket,
                                    hole, board, config);
    auto r2 = SubgameSolver::solve(state, ActionAbstraction(cfg),
                                    kTestBuckets, simpleBucket,
                                    hole, board, config);

    ASSERT_EQ(r1.strategy.size(), r2.strategy.size());
    for (size_t i = 0; i < r1.strategy.size(); ++i)
        EXPECT_FLOAT_EQ(r1.strategy[i], r2.strategy[i]);
}

// ── Actions include fold/call/raise ──────────────────────────────────────

TEST(SubgameSolver, ActionsIncludeFoldCallRaise) {
    initCards();
    GameState state = GameState::newHand(40, 0);
    state = state.withHoleCards(
        cardFromStr("Ah"), cardFromStr("Kh"),
        cardFromStr("Tc"), cardFromStr("9c"));

    BetSizeConfig cfg;
    cfg.preflop = {0.5, 1.0};
    cfg.flop = {0.5, 1.0};
    cfg.turn = {0.5, 1.0};
    cfg.river = {0.5, 1.0};

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {cardFromStr("Ah"), cardFromStr("Kh")};
    hole[1] = {cardFromStr("Tc"), cardFromStr("9c")};
    std::array<Card, 5> board = {
        cardFromStr("5d"), cardFromStr("2c"), cardFromStr("7s"),
        cardFromStr("Jd"), cardFromStr("3h")
    };

    SubgameConfig config;
    config.num_iterations = 50;

    auto result = SubgameSolver::solve(state, ActionAbstraction(cfg),
                                        kTestBuckets, simpleBucket,
                                        hole, board, config);

    // Root is preflop, SB acts first. Should have fold, call, and raise(s).
    bool has_fold = false, has_call = false, has_raise = false;
    for (const auto& a : result.actions) {
        if (a.type == ActionType::Fold) has_fold = true;
        if (a.type == ActionType::Call) has_call = true;
        if (a.type == ActionType::BetRaise) has_raise = true;
    }
    EXPECT_TRUE(has_fold);
    EXPECT_TRUE(has_call);
    EXPECT_TRUE(has_raise);
}
