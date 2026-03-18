#include "abstraction/suit_isomorphism.h"
#include "core/cards.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <set>

// Helper: parse board string like "Ah5d2c" into Card array.
static std::vector<Card> parseBoard(const std::string& s) {
    std::vector<Card> board;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        board.push_back(cardFromStr(s.substr(i, 2)));
    return board;
}

// ── Preflop (no board): all 4 suits are interchangeable ─────────────────

TEST(SuitIsomorphism, PreflopAllSuitsInterchangeable) {
    SuitMapping m = IsomorphismDetector::compute(nullptr, 0);
    EXPECT_EQ(m.num_classes, 1);
    EXPECT_EQ(m.class_size[0], 4);
    // All suits map to the same class.
    EXPECT_EQ(m.suit_class[0], m.suit_class[1]);
    EXPECT_EQ(m.suit_class[1], m.suit_class[2]);
    EXPECT_EQ(m.suit_class[2], m.suit_class[3]);
}

// ── Rainbow flop: 3 suits appear once, 1 suit absent ───────────────────

TEST(SuitIsomorphism, RainbowFlop) {
    auto board = parseBoard("Ah5d2c");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);

    // Two classes: {c,d,h} (freq=1) and {s} (freq=0).
    EXPECT_EQ(m.num_classes, 2);

    // The class with 3 suits has size 3.
    int cls_s = m.suit_class[3];  // spades
    int cls_c = m.suit_class[0];  // clubs
    EXPECT_NE(cls_s, cls_c);
    EXPECT_EQ(m.class_size[cls_c], 3);
    EXPECT_EQ(m.class_size[cls_s], 1);

    // h, d, c all in the same class.
    EXPECT_EQ(m.suit_class[0], m.suit_class[1]);
    EXPECT_EQ(m.suit_class[1], m.suit_class[2]);
}

// ── Two-tone flop: 2 suits appear, 2 absent ────────────────────────────

TEST(SuitIsomorphism, TwoToneFlop) {
    auto board = parseBoard("Ah5h2c");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);

    // h appears 2x, c appears 1x, d and s appear 0x.
    // Three classes: {h}(2), {c}(1), {d,s}(0).
    EXPECT_EQ(m.num_classes, 3);

    int cls_h = m.suit_class[2];  // hearts
    int cls_c = m.suit_class[0];  // clubs
    int cls_d = m.suit_class[1];  // diamonds
    int cls_s = m.suit_class[3];  // spades

    EXPECT_NE(cls_h, cls_c);
    EXPECT_NE(cls_h, cls_d);
    EXPECT_NE(cls_c, cls_d);
    EXPECT_EQ(cls_d, cls_s);  // d and s are interchangeable

    EXPECT_EQ(m.class_size[cls_h], 1);
    EXPECT_EQ(m.class_size[cls_c], 1);
    EXPECT_EQ(m.class_size[cls_d], 2);
}

// ── Monotone flop: all same suit, 3 absent suits interchangeable ────────

TEST(SuitIsomorphism, MonotoneFlop) {
    auto board = parseBoard("Ah5h2h");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);

    // h appears 3x, c/d/s appear 0x.
    // Two classes: {h}(3), {c,d,s}(0).
    EXPECT_EQ(m.num_classes, 2);

    int cls_h = m.suit_class[2];
    int cls_c = m.suit_class[0];
    EXPECT_NE(cls_h, cls_c);
    EXPECT_EQ(m.class_size[cls_h], 1);
    EXPECT_EQ(m.class_size[cls_c], 3);

    EXPECT_EQ(m.suit_class[0], m.suit_class[1]);
    EXPECT_EQ(m.suit_class[1], m.suit_class[3]);
}

// ── Turn: 4 cards, rainbow ──────────────────────────────────────────────

TEST(SuitIsomorphism, TurnRainbow) {
    auto board = parseBoard("Ah5d2c7s");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 4);

    // All 4 suits appear once → all interchangeable.
    EXPECT_EQ(m.num_classes, 1);
    EXPECT_EQ(m.class_size[0], 4);
}

// ── Turn: two-tone ──────────────────────────────────────────────────────

TEST(SuitIsomorphism, TurnTwoTone) {
    auto board = parseBoard("Ah5hTd2c");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 4);

    // h=2, d=1, c=1, s=0. Classes: {h}(2), {d,c}(1), {s}(0).
    EXPECT_EQ(m.num_classes, 3);

    int cls_d = m.suit_class[1];
    int cls_c = m.suit_class[0];
    EXPECT_EQ(cls_d, cls_c);
    EXPECT_EQ(m.class_size[cls_d], 2);
}

// ── River: 5 cards, all different textures ──────────────────────────────

TEST(SuitIsomorphism, RiverFourSuits) {
    auto board = parseBoard("Ah5dTc7s2h");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 5);

    // h=2, d=1, c=1, s=1. Classes: {h}(2), {d,c,s}(1).
    EXPECT_EQ(m.num_classes, 2);
    int cls_h = m.suit_class[2];
    int cls_d = m.suit_class[1];
    EXPECT_NE(cls_h, cls_d);
    EXPECT_EQ(m.class_size[cls_h], 1);
    EXPECT_EQ(m.class_size[cls_d], 3);
}

// ── Canonicalize maps to canonical suit ─────────────────────────────────

TEST(SuitIsomorphism, Canonicalize) {
    auto board = parseBoard("Ah5d2c");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);

    // h, d, c are in the same class. Canonical suit is the first one encountered
    // (clubs, suit=0).
    Card Kh = cardFromStr("Kh");
    Card Kd = cardFromStr("Kd");
    Card Kc = cardFromStr("Kc");
    Card Ks = cardFromStr("Ks");

    Card canon_Kh = IsomorphismDetector::canonicalize(Kh, m);
    Card canon_Kd = IsomorphismDetector::canonicalize(Kd, m);
    Card canon_Kc = IsomorphismDetector::canonicalize(Kc, m);

    // All three should map to the same canonical card.
    EXPECT_EQ(canon_Kh, canon_Kd);
    EXPECT_EQ(canon_Kd, canon_Kc);
    EXPECT_EQ(rankOf(canon_Kh), rankOf(Kh));

    // Spades is in a different class, so Ks canonicalizes differently.
    Card canon_Ks = IsomorphismDetector::canonicalize(Ks, m);
    EXPECT_NE(canon_Ks, canon_Kh);
    EXPECT_EQ(rankOf(canon_Ks), rankOf(Ks));
}

// ── canonicalNextCards reduces card count ────────────────────────────────

TEST(SuitIsomorphism, CanonicalNextCardsReduction) {
    auto board = parseBoard("Ah5d2c");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);

    uint64_t dead = 0;
    for (auto c : board) dead |= (uint64_t(1) << c);

    auto cards = IsomorphismDetector::canonicalNextCards(m, dead);

    // Two classes → 13 ranks * 2 canonical suits = 26 possible canonical cards.
    // Dead mask has actual cards: Ah(50), 5d(13), 2c(0).
    // Of canonical cards (clubs + spades), only 2c(0) is dead.
    // So: 26 - 1 = 25 canonical cards.
    EXPECT_EQ(cards.size(), 25u);

    // Verify no dead cards in result.
    for (auto c : cards)
        EXPECT_EQ(dead & (uint64_t(1) << c), 0u);
}

// ── Multiplicity sums to remaining cards ────────────────────────────────

TEST(SuitIsomorphism, MultiplicitySumsCorrectly) {
    auto board = parseBoard("Ah5d2c");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);

    uint64_t dead = 0;
    for (auto c : board) dead |= (uint64_t(1) << c);

    auto cards = IsomorphismDetector::canonicalNextCards(m, dead);

    int total = 0;
    for (auto c : cards)
        total += IsomorphismDetector::multiplicity(c, m);

    // Total should equal 49 (52 - 3 board cards).
    EXPECT_EQ(total, 49);
}

// ── Expansion permutations: preflop gives 4! = 24 ──────────────────────

TEST(SuitIsomorphism, PreflopExpansion24) {
    SuitMapping m = IsomorphismDetector::compute(nullptr, 0);
    auto perms = IsomorphismDetector::expansionPermutations(m);
    EXPECT_EQ(perms.size(), 24u);  // 4! permutations

    // Each should be a valid permutation of {0,1,2,3}.
    for (const auto& p : perms) {
        std::set<int> vals(p.begin(), p.end());
        EXPECT_EQ(vals.size(), 4u);
        EXPECT_TRUE(vals.count(0) && vals.count(1) && vals.count(2) && vals.count(3));
    }

    // All should be distinct.
    std::set<std::array<int, 4>> unique(perms.begin(), perms.end());
    EXPECT_EQ(unique.size(), 24u);
}

// ── Expansion permutations: rainbow flop gives 3! * 1! = 6 ─────────────

TEST(SuitIsomorphism, RainbowFlopExpansion6) {
    auto board = parseBoard("Ah5d2c");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);
    auto perms = IsomorphismDetector::expansionPermutations(m);
    EXPECT_EQ(perms.size(), 6u);

    // Each is a valid permutation.
    std::set<std::array<int, 4>> unique(perms.begin(), perms.end());
    EXPECT_EQ(unique.size(), 6u);
}

// ── Expansion permutations: monotone flop gives 3! = 6 ─────────────────

TEST(SuitIsomorphism, MonotoneFlopExpansion6) {
    auto board = parseBoard("Ah5h2h");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);
    auto perms = IsomorphismDetector::expansionPermutations(m);
    // {h} alone, {c,d,s} permute → 3! = 6.
    EXPECT_EQ(perms.size(), 6u);
}

// ── Expansion permutations: two-tone flop gives 2! = 2 ─────────────────

TEST(SuitIsomorphism, TwoToneFlopExpansion2) {
    auto board = parseBoard("Ah5hTd");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);
    auto perms = IsomorphismDetector::expansionPermutations(m);
    // Classes: {h}(2), {d}(1), {c,s}(0). Only {c,s} permutes → 2! = 2.
    EXPECT_EQ(perms.size(), 2u);
}

// ── Rainbow turn (all 4 suits): 4! = 24 permutations ───────────────────

TEST(SuitIsomorphism, TurnRainbowExpansion24) {
    auto board = parseBoard("Ah5d2c7s");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 4);
    auto perms = IsomorphismDetector::expansionPermutations(m);
    EXPECT_EQ(perms.size(), 24u);
}

// ── All suits unique on river (h=2, rest=1): no permutations ────────────

TEST(SuitIsomorphism, RiverMostlyUnique) {
    auto board = parseBoard("Ah5dTc7s2h");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 5);
    auto perms = IsomorphismDetector::expansionPermutations(m);
    // {h}(2), {d,c,s}(1) → 3! = 6.
    EXPECT_EQ(perms.size(), 6u);
}

// ── Expansion permutations preserve non-class suits ─────────────────────

TEST(SuitIsomorphism, ExpansionPreservesFixedSuits) {
    auto board = parseBoard("Ah5hTd");
    SuitMapping m = IsomorphismDetector::compute(board.data(), 3);
    auto perms = IsomorphismDetector::expansionPermutations(m);

    // Hearts (class alone, freq=2) and diamonds (class alone, freq=1)
    // should stay fixed in all permutations.
    for (const auto& p : perms) {
        EXPECT_EQ(p[2], 2) << "Hearts should be fixed";
        EXPECT_EQ(p[1], 1) << "Diamonds should be fixed";
    }
}
