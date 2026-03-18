#include "abstraction/action_abstraction.h"
#include "core/cards.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <set>

// Helper: create a state at a specific point
static GameState makePreflop(int stack = 200) {
    auto s = GameState::newHand(stack, 0);
    return s.withHoleCards(0, 1, 2, 3);  // arbitrary cards
}

static GameState makeFlop(int stack = 200) {
    auto s = makePreflop(stack);
    // Limp in: SB calls, BB checks
    s = s.apply(Action::call());
    s = s.apply(Action::check());
    return s.withFlop(4, 5, 6);
}

// ── Default config has 2 fractions per street ──────────────────────────

TEST(ActionAbstraction, DefaultConfigSizes) {
    auto cfg = BetSizeConfig::defaultConfig();
    EXPECT_EQ(cfg.preflop.size(), 2u);
    EXPECT_EQ(cfg.flop.size(), 2u);
    EXPECT_EQ(cfg.turn.size(), 2u);
    EXPECT_EQ(cfg.river.size(), 2u);
    EXPECT_DOUBLE_EQ(cfg.flop[0], 0.5);
    EXPECT_DOUBLE_EQ(cfg.flop[1], 1.0);
}

// ── Preflop: first to act (SB/button) can fold, call, or raise ────────

TEST(ActionAbstraction, PreflopFirstAction) {
    ActionAbstraction aa;
    auto state = makePreflop();

    auto actions = aa.getActions(state);

    // Should have: fold, call, + bet sizes + all-in
    bool has_fold = false, has_call = false, has_allin = false;
    int raise_count = 0;
    for (const auto& a : actions) {
        if (a.type == ActionType::Fold) has_fold = true;
        if (a.type == ActionType::Call) has_call = true;
        if (a.type == ActionType::BetRaise) {
            raise_count++;
            if (a.amount == state.stacks[state.actor] + state.street_bet[state.actor])
                has_allin = true;
        }
    }
    EXPECT_TRUE(has_fold);
    EXPECT_TRUE(has_call);
    EXPECT_TRUE(has_allin) << "All-in should always be included";
    EXPECT_GE(raise_count, 1) << "Should have at least one raise size";
}

// ── Flop: first to act can check or bet ────────────────────────────────

TEST(ActionAbstraction, FlopCheckOrBet) {
    ActionAbstraction aa;
    auto state = makeFlop();

    auto actions = aa.getActions(state);

    bool has_check = false, has_fold = false;
    int bet_count = 0;
    for (const auto& a : actions) {
        if (a.type == ActionType::Check) has_check = true;
        if (a.type == ActionType::Fold) has_fold = true;
        if (a.type == ActionType::BetRaise) bet_count++;
    }
    EXPECT_TRUE(has_check) << "Should be able to check when no bet facing";
    EXPECT_FALSE(has_fold) << "Should not fold when can check";
    EXPECT_GE(bet_count, 1) << "Should have bet sizes";
}

// ── All-in always present when can raise ───────────────────────────────

TEST(ActionAbstraction, AllinAlwaysPresent) {
    ActionAbstraction aa;
    auto state = makeFlop();

    auto actions = aa.getActions(state);
    auto legal = state.legalActions();

    if (legal.can_bet_raise) {
        bool has_allin = false;
        for (const auto& a : actions) {
            if (a.type == ActionType::BetRaise &&
                a.amount == legal.max_bet_raise)
                has_allin = true;
        }
        EXPECT_TRUE(has_allin);
    }
}

// ── No duplicate raise amounts ─────────────────────────────────────────

TEST(ActionAbstraction, NoDuplicateRaises) {
    ActionAbstraction aa;
    auto state = makePreflop();

    auto actions = aa.getActions(state);

    std::set<int> raise_amounts;
    for (const auto& a : actions) {
        if (a.type == ActionType::BetRaise) {
            EXPECT_TRUE(raise_amounts.insert(a.amount).second)
                << "Duplicate raise amount: " << a.amount;
        }
    }
}

// ── Raise amounts are within legal range ───────────────────────────────

TEST(ActionAbstraction, RaisesInLegalRange) {
    ActionAbstraction aa;
    auto state = makePreflop();

    auto actions = aa.getActions(state);
    auto legal = state.legalActions();

    for (const auto& a : actions) {
        if (a.type == ActionType::BetRaise) {
            EXPECT_GE(a.amount, legal.min_bet_raise)
                << "Raise below minimum";
            EXPECT_LE(a.amount, legal.max_bet_raise)
                << "Raise above maximum (all-in)";
        }
    }
}

// ── Raise amounts are sorted ascending ─────────────────────────────────

TEST(ActionAbstraction, RaisesSorted) {
    ActionAbstraction aa;
    auto state = makeFlop();

    auto actions = aa.getActions(state);

    int prev = 0;
    for (const auto& a : actions) {
        if (a.type == ActionType::BetRaise) {
            EXPECT_GT(a.amount, prev)
                << "Raises should be in ascending order";
            prev = a.amount;
        }
    }
}

// ── Custom config: single bet size ─────────────────────────────────────

TEST(ActionAbstraction, CustomSingleSize) {
    BetSizeConfig cfg;
    cfg.preflop = {0.75};
    cfg.flop = {0.75};
    cfg.turn = {0.75};
    cfg.river = {0.75};

    ActionAbstraction aa(cfg);
    auto state = makeFlop();
    auto actions = aa.getActions(state);

    int bet_count = 0;
    for (const auto& a : actions)
        if (a.type == ActionType::BetRaise) bet_count++;

    // Should have: 0.75x pot + all-in = 2 raise sizes
    // (unless 0.75x rounds to all-in, then just 1)
    EXPECT_GE(bet_count, 1);
    EXPECT_LE(bet_count, 2);
}

// ── Custom config: many bet sizes ──────────────────────────────────────

TEST(ActionAbstraction, CustomManySizes) {
    BetSizeConfig cfg;
    cfg.preflop = {0.33, 0.5, 0.75, 1.0, 1.5, 2.0};
    cfg.flop = cfg.preflop;
    cfg.turn = cfg.preflop;
    cfg.river = cfg.preflop;

    ActionAbstraction aa(cfg);
    auto state = makeFlop();
    auto actions = aa.getActions(state);

    int bet_count = 0;
    for (const auto& a : actions)
        if (a.type == ActionType::BetRaise) bet_count++;

    // Up to 6 fractions + all-in = 7, but some may deduplicate
    EXPECT_GE(bet_count, 2);
}

// ── Short stack: all fractions collapse to all-in ──────────────────────

TEST(ActionAbstraction, ShortStackAllCollapse) {
    ActionAbstraction aa;
    // 4 chips = 2bb stack. After blinds (SB=1, BB=2), SB has 3 left.
    auto state = makePreflop(4);

    auto actions = aa.getActions(state);
    auto legal = state.legalActions();

    if (legal.can_bet_raise) {
        int bet_count = 0;
        for (const auto& a : actions)
            if (a.type == ActionType::BetRaise) bet_count++;

        // With tiny stack, all fractions clamp to min or max.
        // Should have at most 2 distinct raises (min-raise and all-in),
        // or just 1 if they're the same.
        EXPECT_GE(bet_count, 1);
    }
}

// ── After a raise, should be able to re-raise ──────────────────────────

TEST(ActionAbstraction, ReraiseActions) {
    ActionAbstraction aa;
    auto state = makePreflop();

    // SB raises
    auto legal = state.legalActions();
    auto actions = aa.getActions(state);

    // Find a non-all-in raise
    Action raise_action = Action::betRaise(legal.min_bet_raise);
    for (const auto& a : actions) {
        if (a.type == ActionType::BetRaise && a.amount < legal.max_bet_raise) {
            raise_action = a;
            break;
        }
    }

    auto state2 = state.apply(raise_action);
    auto actions2 = aa.getActions(state2);

    // BB should be able to fold, call, and re-raise
    bool has_fold = false, has_call = false;
    int raise_count = 0;
    for (const auto& a : actions2) {
        if (a.type == ActionType::Fold) has_fold = true;
        if (a.type == ActionType::Call) has_call = true;
        if (a.type == ActionType::BetRaise) raise_count++;
    }
    EXPECT_TRUE(has_fold);
    EXPECT_TRUE(has_call);
    EXPECT_GE(raise_count, 1);
}

// ── Pot-fraction calculation sanity ────────────────────────────────────

TEST(ActionAbstraction, PotFractionCalculation) {
    BetSizeConfig cfg;
    cfg.preflop = {};  // no fractions, just all-in
    cfg.flop = {1.0};  // pot-sized bet only
    cfg.turn = {1.0};
    cfg.river = {1.0};

    ActionAbstraction aa(cfg);
    auto state = makeFlop();
    // After limp (SB calls 1, BB checks): pot = 4 (2+2 from blinds collected)
    // street_bet = [0, 0], pot = 4
    // Pot-sized bet = 4 chips. Total committed = 0 + 0 + 4 = 4.

    auto actions = aa.getActions(state);

    bool found_pot_bet = false;
    for (const auto& a : actions) {
        if (a.type == ActionType::BetRaise && a.amount != state.legalActions().max_bet_raise) {
            // This should be the pot-sized bet
            int expected = state.potSize();  // pot fraction = 1.0
            EXPECT_EQ(a.amount, expected)
                << "Pot-sized bet should equal potSize()=" << state.potSize();
            found_pot_bet = true;
        }
    }
    EXPECT_TRUE(found_pot_bet) << "Should have a pot-sized bet action";
}

// ── Empty fractions: only all-in ───────────────────────────────────────

TEST(ActionAbstraction, EmptyFractionsOnlyAllin) {
    BetSizeConfig cfg;
    cfg.preflop = {};
    cfg.flop = {};
    cfg.turn = {};
    cfg.river = {};

    ActionAbstraction aa(cfg);
    auto state = makeFlop();
    auto legal = state.legalActions();
    auto actions = aa.getActions(state);

    int bet_count = 0;
    for (const auto& a : actions) {
        if (a.type == ActionType::BetRaise) {
            EXPECT_EQ(a.amount, legal.max_bet_raise);
            bet_count++;
        }
    }
    EXPECT_EQ(bet_count, 1) << "Should have exactly all-in when no fractions";
}
