#include "subgame/action_translation.h"

#include <gtest/gtest.h>
#include <cmath>

// ── Exact match ──────────────────────────────────────────────────────────

TEST(ActionTranslation, ExactMatch) {
    std::vector<int> abstracts = {10, 20, 50, 100};
    auto r = ActionTranslation::translate(abstracts, 20);
    EXPECT_EQ(r.action_lo, 1);
    EXPECT_FLOAT_EQ(r.prob_lo, 1.0f);
    EXPECT_FLOAT_EQ(r.prob_hi, 0.0f);
}

// ── Below smallest → maps to smallest ────────────────────────────────────

TEST(ActionTranslation, BelowSmallest) {
    std::vector<int> abstracts = {10, 20, 50};
    auto r = ActionTranslation::translate(abstracts, 5);
    EXPECT_EQ(r.action_lo, 0);
    EXPECT_FLOAT_EQ(r.prob_lo, 1.0f);
}

// ── Above largest → maps to largest ──────────────────────────────────────

TEST(ActionTranslation, AboveLargest) {
    std::vector<int> abstracts = {10, 20, 50};
    auto r = ActionTranslation::translate(abstracts, 75);
    EXPECT_EQ(r.action_lo, 2);
    EXPECT_FLOAT_EQ(r.prob_lo, 1.0f);
}

// ── Midpoint between two sizes → 50/50 ──────────────────────────────────

TEST(ActionTranslation, Midpoint) {
    std::vector<int> abstracts = {10, 30};
    auto r = ActionTranslation::translate(abstracts, 20);
    EXPECT_EQ(r.action_lo, 0);
    EXPECT_EQ(r.action_hi, 1);
    EXPECT_NEAR(r.prob_lo, 0.5f, 1e-6f);
    EXPECT_NEAR(r.prob_hi, 0.5f, 1e-6f);
}

// ── Closer to lower → higher prob for lower ──────────────────────────────

TEST(ActionTranslation, CloserToLower) {
    std::vector<int> abstracts = {10, 50};
    auto r = ActionTranslation::translate(abstracts, 15);
    EXPECT_EQ(r.action_lo, 0);
    EXPECT_EQ(r.action_hi, 1);
    // P(lo) = (50-15)/(50-10) = 35/40 = 0.875
    EXPECT_NEAR(r.prob_lo, 0.875f, 1e-5f);
    EXPECT_NEAR(r.prob_hi, 0.125f, 1e-5f);
}

// ── Closer to upper → higher prob for upper ──────────────────────────────

TEST(ActionTranslation, CloserToUpper) {
    std::vector<int> abstracts = {10, 50};
    auto r = ActionTranslation::translate(abstracts, 45);
    EXPECT_EQ(r.action_lo, 0);
    EXPECT_EQ(r.action_hi, 1);
    // P(lo) = (50-45)/(50-10) = 5/40 = 0.125
    EXPECT_NEAR(r.prob_lo, 0.125f, 1e-5f);
    EXPECT_NEAR(r.prob_hi, 0.875f, 1e-5f);
}

// ── Probabilities sum to 1 ───────────────────────────────────────────────

TEST(ActionTranslation, ProbsSumToOne) {
    std::vector<int> abstracts = {10, 25, 50, 100};
    for (int x = 10; x <= 100; ++x) {
        auto r = ActionTranslation::translate(abstracts, x);
        EXPECT_NEAR(r.prob_lo + r.prob_hi, 1.0f, 1e-5f)
            << "Failed for x=" << x;
    }
}

// ── Three sizes: falls in second bracket ─────────────────────────────────

TEST(ActionTranslation, ThreeSizesMiddleBracket) {
    std::vector<int> abstracts = {10, 30, 100};
    auto r = ActionTranslation::translate(abstracts, 50);
    EXPECT_EQ(r.action_lo, 1);  // 30
    EXPECT_EQ(r.action_hi, 2);  // 100
    // P(lo) = (100-50)/(100-30) = 50/70 ≈ 0.714
    EXPECT_NEAR(r.prob_lo, 50.0f / 70.0f, 1e-5f);
}

// ── Boundary: exactly at lower abstract size ─────────────────────────────

TEST(ActionTranslation, BoundaryLower) {
    std::vector<int> abstracts = {10, 50};
    auto r = ActionTranslation::translate(abstracts, 10);
    EXPECT_EQ(r.action_lo, 0);
    EXPECT_FLOAT_EQ(r.prob_lo, 1.0f);
}

// ── Boundary: exactly at upper abstract size ─────────────────────────────

TEST(ActionTranslation, BoundaryUpper) {
    std::vector<int> abstracts = {10, 50};
    auto r = ActionTranslation::translate(abstracts, 50);
    EXPECT_EQ(r.action_lo, 1);
    EXPECT_FLOAT_EQ(r.prob_lo, 1.0f);
}

// ── translateAction: fold maps directly ──────────────────────────────────

TEST(ActionTranslation, FoldMapsDirect) {
    std::vector<Action> abstracts = {
        Action::fold(), Action::call(), Action::betRaise(20)
    };
    auto r = ActionTranslation::translateAction(Action::fold(), abstracts);
    EXPECT_EQ(r.action_lo, 0);
    EXPECT_FLOAT_EQ(r.prob_lo, 1.0f);
}

// ── translateAction: call maps directly ──────────────────────────────────

TEST(ActionTranslation, CallMapsDirect) {
    std::vector<Action> abstracts = {
        Action::fold(), Action::call(), Action::betRaise(20)
    };
    auto r = ActionTranslation::translateAction(Action::call(), abstracts);
    EXPECT_EQ(r.action_lo, 1);
    EXPECT_FLOAT_EQ(r.prob_lo, 1.0f);
}

// ── translateAction: bet/raise translates amount ─────────────────────────

TEST(ActionTranslation, BetRaiseTranslates) {
    std::vector<Action> abstracts = {
        Action::fold(), Action::call(),
        Action::betRaise(10), Action::betRaise(50)
    };
    auto r = ActionTranslation::translateAction(Action::betRaise(30), abstracts);
    EXPECT_EQ(r.action_lo, 2);  // betRaise(10)
    EXPECT_EQ(r.action_hi, 3);  // betRaise(50)
    EXPECT_NEAR(r.prob_lo, 0.5f, 1e-5f);
    EXPECT_NEAR(r.prob_hi, 0.5f, 1e-5f);
}

// ── translateAction: exact bet match ─────────────────────────────────────

TEST(ActionTranslation, BetExactMatch) {
    std::vector<Action> abstracts = {
        Action::fold(), Action::call(),
        Action::betRaise(10), Action::betRaise(50)
    };
    auto r = ActionTranslation::translateAction(Action::betRaise(50), abstracts);
    EXPECT_EQ(r.action_lo, 3);
    EXPECT_FLOAT_EQ(r.prob_lo, 1.0f);
}
