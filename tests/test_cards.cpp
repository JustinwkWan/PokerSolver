#include "core/cards.h"

#include <gtest/gtest.h>
#include <set>
#include <string>

// ── Card encoding ──────────────────────────────────────────────────────────

TEST(Cards, RankSuitRoundtrip) {
    for (int rank = 0; rank < kNumRanks; ++rank) {
        for (int suit = 0; suit < kNumSuits; ++suit) {
            Card c = makeCard(rank, suit);
            EXPECT_EQ(rankOf(c), rank);
            EXPECT_EQ(suitOf(c), suit);
            EXPECT_LT(c, kNumCards);
        }
    }
}

TEST(Cards, AllCardsUnique) {
    std::set<Card> seen;
    for (int rank = 0; rank < kNumRanks; ++rank)
        for (int suit = 0; suit < kNumSuits; ++suit)
            seen.insert(makeCard(rank, suit));
    EXPECT_EQ(seen.size(), 52u);
}

TEST(Cards, StringRoundtrip) {
    for (Card c = 0; c < kNumCards; ++c) {
        std::string s = cardToStr(c);
        EXPECT_EQ(s.size(), 2u);
        Card back = cardFromStr(s);
        EXPECT_EQ(back, c) << "Failed for " << s;
    }
}

TEST(Cards, KnownCards) {
    // Ace of spades
    Card as = cardFromStr("As");
    EXPECT_EQ(rankOf(as), 12);
    EXPECT_EQ(suitOf(as), 3);

    // Two of clubs
    Card tc = cardFromStr("2c");
    EXPECT_EQ(rankOf(tc), 0);
    EXPECT_EQ(suitOf(tc), 0);

    // Ten of hearts
    Card th = cardFromStr("Th");
    EXPECT_EQ(rankOf(th), 8);
    EXPECT_EQ(suitOf(th), 2);
}

// ── Deck ───────────────────────────────────────────────────────────────────

TEST(Deck, DealAll52) {
    std::mt19937_64 rng(42);
    Deck d;
    d.shuffle(rng);

    std::set<Card> dealt;
    while (d.remaining() > 0) dealt.insert(d.deal());

    EXPECT_EQ(dealt.size(), 52u);
    for (Card c = 0; c < kNumCards; ++c)
        EXPECT_TRUE(dealt.count(c)) << "Missing card " << cardToStr(c);
}

TEST(Deck, Reset) {
    Deck d;
    d.deal(); d.deal();
    EXPECT_EQ(d.remaining(), 50);
    d.reset();
    EXPECT_EQ(d.remaining(), 52);
}

TEST(Deck, ShuffleProducesVariation) {
    std::mt19937_64 rng1(1), rng2(2);
    Deck d1, d2;
    d1.shuffle(rng1);
    d2.shuffle(rng2);

    int same = 0;
    for (int i = 0; i < 52; ++i)
        if (d1.deal() == d2.deal()) same++;

    // Two independent shuffles match in ≤8 positions with overwhelming probability
    EXPECT_LT(same, 9);
}

// ── Canonical preflop hands ────────────────────────────────────────────────

TEST(CanonicalHand, Pairs) {
    // AA: any two aces → index 0
    Card ah = cardFromStr("Ah"), ad = cardFromStr("Ad");
    EXPECT_EQ(canonicalPreflopHand(ah, ad), 0);
    EXPECT_EQ(canonicalPreflopHand(ad, ah), 0);

    // KK → index 1
    Card kh = cardFromStr("Kh"), kd = cardFromStr("Kd");
    EXPECT_EQ(canonicalPreflopHand(kh, kd), 1);

    // 22 → index 12
    Card tc = cardFromStr("2c"), td = cardFromStr("2d");
    EXPECT_EQ(canonicalPreflopHand(tc, td), 12);
}

TEST(CanonicalHand, Suited) {
    // AKs → index 13
    Card ah = cardFromStr("Ah"), kh = cardFromStr("Kh");
    EXPECT_EQ(canonicalPreflopHand(ah, kh), 13);

    // 32s → index 90
    Card tc = cardFromStr("3c"), tc2 = cardFromStr("2c");
    EXPECT_EQ(canonicalPreflopHand(tc, tc2), 90);
}

TEST(CanonicalHand, Offsuit) {
    // AKo → index 91
    Card ah = cardFromStr("Ah"), kd = cardFromStr("Kd");
    EXPECT_EQ(canonicalPreflopHand(ah, kd), 91);

    // 32o → index 168
    Card tc = cardFromStr("3c"), td = cardFromStr("2d");
    EXPECT_EQ(canonicalPreflopHand(tc, td), 168);
}

TEST(CanonicalHand, Exactly169Distinct) {
    initCards();
    std::set<int> distinct;
    for (int i = 0; i < kNumCards; ++i)
        for (int j = 0; j < kNumCards; ++j)
            if (i != j)
                distinct.insert(gCanonicalHandTable[i][j]);

    EXPECT_EQ(distinct.size(), 169u);
    EXPECT_EQ(*distinct.begin(), 0);
    EXPECT_EQ(*distinct.rbegin(), 168);
}

TEST(CanonicalHand, Symmetry) {
    // canonicalPreflopHand(c1, c2) == canonicalPreflopHand(c2, c1)
    for (int i = 0; i < kNumCards; ++i)
        for (int j = i + 1; j < kNumCards; ++j) {
            Card c1(i), c2(j);
            EXPECT_EQ(canonicalPreflopHand(c1, c2), canonicalPreflopHand(c2, c1));
        }
}
