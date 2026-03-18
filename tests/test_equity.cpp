#include "abstraction/equity_calculator.h"
#include "core/cards.h"

#include <gtest/gtest.h>
#include <cmath>
#include <set>

static EquityCalculator& calc() {
    static EquityCalculator c;
    return c;
}

// Helper: make card from string like "Ah", "Td", "2c"
static Card c(const char* s) { return cardFromStr(s); }

// ── Dead card count verification ─────────────────────────────────────────
// riverEquityExact enumerates C(45,2) = 990 opponent combos internally.
// We verify indirectly: call it with a known hand and check the result
// is in [0,1] (the internal assert checks count == 990).

TEST(Equity, RiverExactReturnsValidRange) {
    std::array<Card, 2> hole = {c("Ah"), c("Kh")};
    std::array<Card, 5> board = {c("2c"), c("5d"), c("8s"), c("3h"), c("9c")};
    double eq = calc().riverEquityExact(hole, board);
    EXPECT_GE(eq, 0.0);
    EXPECT_LE(eq, 1.0);
}

// ── Nut hand should have equity ~1.0 ────────────────────────────────────
// Royal flush on board: Ah Kh Qh Jh Th. Any hole cards that include a
// heart above T would share, but hero with royal flush should win or tie
// against nearly all opponents.

TEST(Equity, NutFlushHighEquity) {
    // Hero: Ah Kh on board 5h 8h 2h 9d Tc — hero has nut flush (3 board hearts)
    std::array<Card, 2> hole = {c("Ah"), c("Kh")};
    std::array<Card, 5> board = {c("5h"), c("8h"), c("2h"), c("9d"), c("Tc")};
    double eq = calc().riverEquityExact(hole, board);
    // Nut flush loses only to straight flush (none possible on this board)
    EXPECT_GT(eq, 0.95);
}

// ── Known matchup: top pair vs underpair ─────────────────────────────────
// On a dry board, top pair should beat an underpair most of the time.

TEST(Equity, TopPairVsUnderpair) {
    // AK on A-high board vs QQ: AK should dominate
    std::array<Card, 5> board = {c("As"), c("7d"), c("3c"), c("9h"), c("2s")};
    double eq_ak = calc().riverEquityExact({c("Ah"), c("Kd")}, board);
    double eq_qq = calc().riverEquityExact({c("Qh"), c("Qd")}, board);
    EXPECT_GT(eq_ak, eq_qq);
}

// ── Identical hands = 0.5 equity ─────────────────────────────────────────
// Two players with the same effective hand (different suits, same rank)
// should have equal equity = 0.5 (against the same opponent range).

TEST(Equity, SameRankHandsEqualEquity) {
    std::array<Card, 5> board = {c("2c"), c("5d"), c("8s"), c("Th"), c("Ks")};
    // AhQh vs AdQd — same ranks, different suits, board has no flush draw
    double eq1 = calc().riverEquityExact({c("Ah"), c("Qh")}, board);
    double eq2 = calc().riverEquityExact({c("Ad"), c("Qd")}, board);
    // Not exactly equal because suit interactions with board differ slightly,
    // but should be very close on a rainbow board.
    EXPECT_NEAR(eq1, eq2, 0.01);
}

// ── allRiverEquities consistency ─────────────────────────────────────────
// Spot-check: allRiverEquities should match individual riverEquityExact calls.

TEST(Equity, BatchMatchesIndividual) {
    std::array<Card, 5> board = {c("4c"), c("7d"), c("Jh"), c("2s"), c("Qs")};
    auto all = calc().allRiverEquities(board);

    // Should have C(47,2) = 1081 entries
    EXPECT_EQ(static_cast<int>(all.size()), 1081);

    // Spot-check first 5 and last 5
    for (int i = 0; i < 5; ++i) {
        double expected = calc().riverEquityExact(all[i].hole, board);
        EXPECT_DOUBLE_EQ(all[i].equity, expected);
    }
    for (int i = static_cast<int>(all.size()) - 5;
         i < static_cast<int>(all.size()); ++i) {
        double expected = calc().riverEquityExact(all[i].hole, board);
        EXPECT_DOUBLE_EQ(all[i].equity, expected);
    }
}

// ── All equities in [0, 1] ──────────────────────────────────────────────
TEST(Equity, AllEquitiesInRange) {
    std::array<Card, 5> board = {c("4c"), c("7d"), c("Jh"), c("2s"), c("Qs")};
    auto all = calc().allRiverEquities(board);
    for (const auto& he : all) {
        EXPECT_GE(he.equity, 0.0);
        EXPECT_LE(he.equity, 1.0);
    }
}

// ── No duplicate hole cards in batch ────────────────────────────────────
TEST(Equity, NoDuplicatesInBatch) {
    std::array<Card, 5> board = {c("4c"), c("7d"), c("Jh"), c("2s"), c("Qs")};
    auto all = calc().allRiverEquities(board);

    std::set<std::pair<Card, Card>> seen;
    for (const auto& he : all) {
        Card lo = std::min(he.hole[0], he.hole[1]);
        Card hi = std::max(he.hole[0], he.hole[1]);
        EXPECT_TRUE(seen.insert({lo, hi}).second)
            << "Duplicate hole cards: " << cardToStr(lo) << cardToStr(hi);
    }
}

// ── Monte Carlo: determinism with same seed ──────────────────────────────
TEST(Equity, MonteCarloIsDeterministic) {
    std::array<Card, 2> hole = {c("Ah"), c("Ks")};
    Card board[3] = {c("2c"), c("5d"), c("8h")};

    std::mt19937_64 rng1(42);
    double eq1 = calc().equityMonteCarlo(hole, board, 3, 5000, rng1);

    std::mt19937_64 rng2(42);
    double eq2 = calc().equityMonteCarlo(hole, board, 3, 5000, rng2);

    EXPECT_DOUBLE_EQ(eq1, eq2);
}

// ── Monte Carlo: AA vs random preflop ≈ 85% ─────────────────────────────
TEST(Equity, MonteCarloAAPreflopEquity) {
    std::array<Card, 2> hole = {c("Ah"), c("As")};
    // No board cards (preflop): num_board = 0
    std::mt19937_64 rng(123);
    double eq = calc().equityMonteCarlo(hole, nullptr, 0, 50000, rng);
    // AA preflop vs random hand equity is ~85.2%
    EXPECT_NEAR(eq, 0.852, 0.02);
}

// ── Monte Carlo: flop equity in valid range ──────────────────────────────
TEST(Equity, MonteCarloFlopRange) {
    std::array<Card, 2> hole = {c("Th"), c("9h")};
    Card board[3] = {c("8h"), c("7c"), c("2d")};
    std::mt19937_64 rng(999);
    double eq = calc().equityMonteCarlo(hole, board, 3, 10000, rng);
    EXPECT_GE(eq, 0.0);
    EXPECT_LE(eq, 1.0);
    // T9 with open-ended straight draw should have decent equity
    EXPECT_GT(eq, 0.3);
}
