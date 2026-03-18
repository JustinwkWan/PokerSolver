#include "core/game_state.h"
#include "core/hand_evaluator.h"
#include "core/cards.h"

#include <gtest/gtest.h>
#include <random>

// ── Helpers ────────────────────────────────────────────────────────────────

static Card c(const char* s) { return cardFromStr(s); }

// Pick a random legal action for automated playouts
static Action randomAction(const GameState& s, std::mt19937_64& rng) {
    LegalActionInfo info = s.legalActions();

    // Collect options
    std::vector<Action> options;
    if (info.can_fold)  options.push_back(Action::fold());
    if (info.can_check) options.push_back(Action::check());
    if (info.can_call)  options.push_back(Action::call());
    if (info.can_bet_raise) {
        // Pick a random bet size between min and max
        std::uniform_int_distribution<int> dist(info.min_bet_raise,
                                                info.max_bet_raise);
        options.push_back(Action::betRaise(dist(rng)));
    }

    assert(!options.empty());
    std::uniform_int_distribution<size_t> pick(0, options.size() - 1);
    return options[pick(rng)];
}

// Deal remaining board cards (removes dealt cards from a copy of the deck)
static GameState dealBoard(GameState s, Deck& deck) {
    if (!s.needsCards()) return s;

    if (s.street == Street::Flop) {
        s = s.withFlop(deck.deal(), deck.deal(), deck.deal());
    } else if (s.street == Street::Turn) {
        s = s.withTurn(deck.deal());
    } else if (s.street == Street::River) {
        s = s.withRiver(deck.deal());
    }
    return s;
}

// Play one complete hand; returns true without asserting (for playout tests)
static bool playRandomHand(std::mt19937_64& rng, int stack_bb = 100) {
    Deck deck;
    deck.shuffle(rng);

    std::uniform_int_distribution<int> button_dist(0, 1);
    GameState s = GameState::newHand(stack_bb * kBB, button_dist(rng));

    // Deal hole cards
    s = s.withHoleCards(deck.deal(), deck.deal(), deck.deal(), deck.deal());

    int steps = 0;
    while (!s.isTerminal()) {
        if (s.needsCards()) {
            s = dealBoard(s, deck);
            continue;
        }
        s = s.apply(randomAction(s, rng));
        if (++steps > 200) return false;  // runaway guard
    }
    return true;
}

// ── Hand construction tests ────────────────────────────────────────────────

TEST(GameState, InitialBlinds) {
    // button = 0 → SB = player 0, BB = player 1
    GameState s = GameState::newHand(200, 0);

    EXPECT_EQ(s.stacks[0], 200 - kSB);  // SB posted
    EXPECT_EQ(s.stacks[1], 200 - kBB);  // BB posted
    EXPECT_EQ(s.street_bet[0], kSB);
    EXPECT_EQ(s.street_bet[1], kBB);
    EXPECT_EQ(s.actor, 0);              // SB acts first preflop
    EXPECT_EQ(s.street, Street::Preflop);
    EXPECT_FALSE(s.isTerminal());
    EXPECT_FALSE(s.needsCards());       // hole cards not dealt yet
}

TEST(GameState, FoldPreflop) {
    GameState s = GameState::newHand(200, 0);
    s = s.withHoleCards(c("Ah"), c("Kh"), c("2c"), c("3d"));
    s = s.apply(Action::fold());

    EXPECT_TRUE(s.isTerminal());
    EXPECT_EQ(s.foldWinner(), 1);  // BB wins
}

TEST(GameState, PreflopCallBBOption) {
    // SB calls → BB gets the option (check or raise)
    GameState s = GameState::newHand(200, 0);
    s = s.withHoleCards(c("Ah"), c("Kh"), c("2c"), c("3d"));

    // SB calls (puts in 1 more chip to match BB = 2)
    s = s.apply(Action::call());

    EXPECT_EQ(s.actor, 1);          // BB's turn (option)
    EXPECT_FALSE(s.isTerminal());
    EXPECT_FALSE(s.needsCards());
    EXPECT_EQ(s.street, Street::Preflop);

    LegalActionInfo info = s.legalActions();
    EXPECT_TRUE(info.can_check);    // BB can check (close action)
    EXPECT_TRUE(info.can_bet_raise); // BB can raise

    // BB checks → go to flop
    s = s.apply(Action::check());
    EXPECT_TRUE(s.needsCards());
    EXPECT_EQ(s.street, Street::Flop);
}

TEST(GameState, PotAfterPreflopCallCheck) {
    GameState s = GameState::newHand(200, 0);
    s = s.withHoleCards(c("Ah"), c("Kh"), c("2c"), c("3d"));
    s = s.apply(Action::call());   // SB calls (total 2)
    s = s.apply(Action::check());  // BB checks

    // Both committed 2 chips → pot = 4
    EXPECT_EQ(s.pot, 4);
    EXPECT_EQ(s.street_bet[0], 0);
    EXPECT_EQ(s.street_bet[1], 0);
}

TEST(GameState, Raise3bet) {
    GameState s = GameState::newHand(200, 0);
    s = s.withHoleCards(c("Ah"), c("Kh"), c("2c"), c("3d"));

    // SB raises to 6 (total committed = 6)
    s = s.apply(Action::betRaise(6));
    EXPECT_EQ(s.street_bet[0], 6);
    EXPECT_EQ(s.stacks[0], 200 - kSB - (6 - kSB));  // 200 - 6 = 194

    // BB 3-bets to 18
    s = s.apply(Action::betRaise(18));
    EXPECT_EQ(s.street_bet[1], 18);

    // SB calls
    s = s.apply(Action::call());
    EXPECT_TRUE(s.needsCards());  // go to flop
    EXPECT_EQ(s.pot, 18 * 2);
}

TEST(GameState, AllIn) {
    // Tiny stack to force all-in quickly
    GameState s = GameState::newHand(4, 0);  // 2bb stacks
    s = s.withHoleCards(c("Ah"), c("Kh"), c("2c"), c("3d"));

    // SB is already almost all-in: stacks[SB] = 4 - 1 = 3
    LegalActionInfo info = s.legalActions();
    EXPECT_TRUE(info.can_bet_raise);

    s = s.apply(Action::betRaise(info.max_bet_raise)); // SB shoves
    EXPECT_TRUE(s.all_in[0]);
}

TEST(GameState, PostflopOOPActsFirst) {
    GameState s = GameState::newHand(200, 0);  // button = 0
    s = s.withHoleCards(c("Ah"), c("Kh"), c("2c"), c("3d"));
    s = s.apply(Action::call());   // SB calls
    s = s.apply(Action::check());  // BB checks → flop

    s = s.withFlop(c("7h"), c("8d"), c("9c"));

    // OOP (non-button = player 1) acts first postflop
    EXPECT_EQ(s.actor, 1);
}

TEST(GameState, FullHandToShowdown) {
    GameState s = GameState::newHand(200, 0);
    s = s.withHoleCards(c("Ah"), c("Kh"), c("2c"), c("7d"));
    s = s.apply(Action::call());
    s = s.apply(Action::check());

    s = s.withFlop(c("Qh"), c("Jh"), c("Th"));
    s = s.apply(Action::check());
    s = s.apply(Action::check());

    s = s.withTurn(c("2h"));
    s = s.apply(Action::check());
    s = s.apply(Action::check());

    s = s.withRiver(c("3d"));
    s = s.apply(Action::check());
    s = s.apply(Action::check());

    EXPECT_TRUE(s.isTerminal());
    EXPECT_EQ(s.foldWinner(), -1);  // showdown
}

// ── Invariant checks ───────────────────────────────────────────────────────

TEST(GameState, StackConservation) {
    // At all times: stacks[0] + stacks[1] + potSize() == stack_size * 2
    GameState s = GameState::newHand(200, 0);
    s = s.withHoleCards(c("Ah"), c("Kh"), c("2c"), c("7d"));

    auto checkInv = [&](const GameState& st) {
        int total = st.stacks[0] + st.stacks[1] + st.potSize();
        EXPECT_EQ(total, st.stack_size * 2) << "Stack conservation violated";
    };

    checkInv(s);
    s = s.apply(Action::betRaise(8));  checkInv(s);
    s = s.apply(Action::call());        checkInv(s);  // flop
    s = s.withFlop(c("Qh"), c("Jh"), c("Th")); checkInv(s);
    s = s.apply(Action::check());      checkInv(s);
    s = s.apply(Action::betRaise(s.legalActions().min_bet_raise)); checkInv(s);
    s = s.apply(Action::fold());       checkInv(s);
}

// ── Random game playout stress test ───────────────────────────────────────
// 10M hands with zero crashes or assertion failures = passing criterion.

TEST(GameState, TenMillionRandomHands) {
    constexpr int kHands = 10'000'000;

    std::mt19937_64 rng(12345);
    int ok = 0;
    for (int i = 0; i < kHands; ++i) {
        ASSERT_TRUE(playRandomHand(rng)) << "Hand " << i << " did not terminate";
        ++ok;
    }
    EXPECT_EQ(ok, kHands);
}
