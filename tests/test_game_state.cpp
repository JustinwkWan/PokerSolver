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

// ═══════════════════════════════════════════════════════════════════════════
// ── Multi-way (N-player) tests ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

// Play a random multi-way hand; returns true if it terminates properly.
static bool playRandomMultiwayHand(std::mt19937_64& rng, int num_players,
                                    int stack_bb = 100) {
    Deck deck;
    deck.shuffle(rng);

    std::uniform_int_distribution<int> button_dist(0, num_players - 1);
    GameState s = GameState::newHand(stack_bb * kBB, button_dist(rng), num_players);

    // Deal hole cards
    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    for (int i = 0; i < num_players; ++i) {
        hole[i][0] = deck.deal();
        hole[i][1] = deck.deal();
    }
    s = s.withHoleCards(hole);

    int steps = 0;
    while (!s.isTerminal()) {
        if (s.needsCards()) {
            s = dealBoard(s, deck);
            continue;
        }
        s = s.apply(randomAction(s, rng));
        if (++steps > 500) return false;  // runaway guard
    }

    // Verify stack conservation
    int total = 0;
    for (int i = 0; i < num_players; ++i)
        total += s.stacks[i];
    total += s.potSize();
    if (total != s.stack_size * num_players) return false;

    return true;
}

TEST(GameState, ThreePlayerBlinds) {
    // button = 0, SB = 1, BB = 2, UTG = 0 (wraps)
    GameState s = GameState::newHand(200, 0, 3);

    EXPECT_EQ(s.num_players, 3);
    EXPECT_EQ(s.street_bet[1], kSB);  // SB = seat 1
    EXPECT_EQ(s.street_bet[2], kBB);  // BB = seat 2
    EXPECT_EQ(s.street_bet[0], 0);    // BTN hasn't posted
    EXPECT_EQ(s.actor, 0);            // UTG = (0+3)%3 = 0 = BTN in 3p
}

TEST(GameState, SixPlayerBlinds) {
    // button = 3, SB = 4, BB = 5, UTG = 0
    GameState s = GameState::newHand(200, 3, 6);

    EXPECT_EQ(s.num_players, 6);
    EXPECT_EQ(s.street_bet[4], kSB);  // SB = seat 4
    EXPECT_EQ(s.street_bet[5], kBB);  // BB = seat 5
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(s.street_bet[i], 0);
    EXPECT_EQ(s.actor, 0);            // UTG = (3+3)%6 = 0
}

TEST(GameState, ThreePlayerPreflopActionOrder) {
    // button=0, SB=1, BB=2, UTG=0
    // In 3-player, UTG = button. UTG acts first, then SB, then BB.
    GameState s = GameState::newHand(200, 0, 3);

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {c("Ah"), c("Kh")};
    hole[1] = {c("2c"), c("3d")};
    hole[2] = {c("4s"), c("5s")};
    s = s.withHoleCards(hole);

    EXPECT_EQ(s.actor, 0);  // UTG = seat 0
    s = s.apply(Action::call());  // UTG calls
    EXPECT_EQ(s.actor, 1);  // SB
    s = s.apply(Action::call());  // SB calls
    EXPECT_EQ(s.actor, 2);  // BB option
    EXPECT_FALSE(s.isTerminal());
}

TEST(GameState, MultiwayFoldToWinner) {
    // 4 players, everyone folds except one
    GameState s = GameState::newHand(200, 0, 4);
    // SB=1, BB=2, UTG=3

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {c("Ah"), c("Kh")};
    hole[1] = {c("2c"), c("3d")};
    hole[2] = {c("4s"), c("5s")};
    hole[3] = {c("6h"), c("7h")};
    s = s.withHoleCards(hole);

    EXPECT_EQ(s.actor, 3);  // UTG
    s = s.apply(Action::fold());
    EXPECT_FALSE(s.isTerminal());

    EXPECT_EQ(s.actor, 0);  // BTN
    s = s.apply(Action::fold());
    EXPECT_FALSE(s.isTerminal());

    EXPECT_EQ(s.actor, 1);  // SB
    s = s.apply(Action::fold());
    EXPECT_TRUE(s.isTerminal());
    EXPECT_EQ(s.foldWinner(), 2);  // BB wins
}

TEST(GameState, ThreeWayToFlop) {
    // 3 players all limp to flop
    GameState s = GameState::newHand(200, 0, 3);

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {c("Ah"), c("Kh")};
    hole[1] = {c("2c"), c("3d")};
    hole[2] = {c("4s"), c("5s")};
    s = s.withHoleCards(hole);

    // UTG (seat 0) calls, SB (seat 1) calls, BB (seat 2) checks
    s = s.apply(Action::call());   // UTG
    s = s.apply(Action::call());   // SB
    s = s.apply(Action::check());  // BB option

    EXPECT_EQ(s.street, Street::Flop);
    EXPECT_TRUE(s.needsCards());
    EXPECT_EQ(s.pot, kBB * 3);  // 3 players * 2 chips = 6

    s = s.withFlop(c("7h"), c("8d"), c("9c"));

    // Postflop: first active player after button (seat 0)
    // nextActivePlayer(0) = 1 (SB)
    EXPECT_EQ(s.actor, 1);  // SB acts first postflop
}

TEST(GameState, IsBettingClosedPartialFolds) {
    // 4 players, 2 fold preflop, remaining 2 play postflop
    GameState s = GameState::newHand(200, 0, 4);

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {c("Ah"), c("Kh")};
    hole[1] = {c("2c"), c("3d")};
    hole[2] = {c("4s"), c("5s")};
    hole[3] = {c("6h"), c("7h")};
    s = s.withHoleCards(hole);

    // UTG=3 folds, BTN=0 folds, SB=1 calls, BB=2 checks
    s = s.apply(Action::fold());   // UTG folds
    s = s.apply(Action::fold());   // BTN folds
    s = s.apply(Action::call());   // SB calls
    s = s.apply(Action::check());  // BB checks → flop

    EXPECT_EQ(s.street, Street::Flop);
    EXPECT_EQ(s.numNonFolded(), 2);
}

TEST(GameState, ThreePlayerStackConservation) {
    // Verify stack conservation in a 3-player hand
    GameState s = GameState::newHand(200, 0, 3);

    std::array<std::array<Card, 2>, kMaxPlayers> hole{};
    hole[0] = {c("Ah"), c("Kh")};
    hole[1] = {c("2c"), c("3d")};
    hole[2] = {c("4s"), c("5s")};
    s = s.withHoleCards(hole);

    auto checkInv = [&](const GameState& st) {
        int total = st.potSize();
        for (int i = 0; i < st.num_players; ++i) total += st.stacks[i];
        EXPECT_EQ(total, st.stack_size * st.num_players)
            << "Stack conservation violated";
    };

    checkInv(s);
    s = s.apply(Action::betRaise(8));  // UTG raises
    checkInv(s);
    s = s.apply(Action::fold());       // SB folds
    checkInv(s);
    s = s.apply(Action::call());       // BB calls
    checkInv(s);
}

TEST(GameState, ThreePlayerRandomPlayout) {
    std::mt19937_64 rng(54321);
    constexpr int kHands = 100'000;
    for (int i = 0; i < kHands; ++i) {
        ASSERT_TRUE(playRandomMultiwayHand(rng, 3))
            << "3-player hand " << i << " did not terminate";
    }
}

TEST(GameState, SixPlayerRandomPlayout) {
    std::mt19937_64 rng(67890);
    constexpr int kHands = 100'000;
    for (int i = 0; i < kHands; ++i) {
        ASSERT_TRUE(playRandomMultiwayHand(rng, 6))
            << "6-player hand " << i << " did not terminate";
    }
}
