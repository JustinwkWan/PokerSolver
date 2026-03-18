#include "core/hand_evaluator.h"
#include "core/cards.h"

#include <gtest/gtest.h>
#include <array>

// Global evaluator (construction is expensive — do it once)
static HandEvaluator& eval() {
    static HandEvaluator e;
    return e;
}

static Card c(const char* s) { return cardFromStr(s); }

// ── Ordering tests ────────────────────────────────────────────────────────

TEST(HandEval, RoyalFlushBeatsEverything) {
    // Royal flush: Ah Kh Qh Jh Th + two rags
    std::array<Card, 7> rf = {c("Ah"), c("Kh"), c("Qh"), c("Jh"), c("Th"), c("2c"), c("3d")};
    uint16_t rf_rank = eval().evaluate(rf.data(), 7);

    // Pair of aces
    std::array<Card, 7> pair = {c("Ac"), c("Ad"), c("2h"), c("3c"), c("5d"), c("7s"), c("9h")};
    uint16_t pair_rank = eval().evaluate(pair.data(), 7);

    EXPECT_GT(rf_rank, pair_rank);
}

TEST(HandEval, HandRankOrdering) {
    // straight flush > four of a kind > full house > flush > straight > trips > 2pair > pair > high
    auto rank7 = [&](std::array<Card,7> cards) {
        return eval().evaluate(cards.data(), 7);
    };

    uint16_t sf    = rank7({c("9h"), c("8h"), c("7h"), c("6h"), c("5h"), c("2c"), c("3d")});
    uint16_t quads = rank7({c("Ac"), c("Ad"), c("Ah"), c("As"), c("Kh"), c("2c"), c("3d")});
    uint16_t fh    = rank7({c("Ac"), c("Ad"), c("Ah"), c("Ks"), c("Kh"), c("2c"), c("3d")});
    uint16_t flush = rank7({c("Ac"), c("Kc"), c("Tc"), c("6c"), c("2c"), c("3d"), c("4h")});
    uint16_t str8  = rank7({c("9c"), c("8d"), c("7h"), c("6s"), c("5c"), c("2h"), c("3d")});
    uint16_t trips = rank7({c("Ac"), c("Ad"), c("Ah"), c("2s"), c("3c"), c("4d"), c("5h")});
    uint16_t twop  = rank7({c("Ac"), c("Ad"), c("Kh"), c("Ks"), c("2c"), c("3d"), c("4h")});
    uint16_t onep  = rank7({c("Ac"), c("Ad"), c("2h"), c("3s"), c("5c"), c("7d"), c("9h")});
    uint16_t hcard = rank7({c("Ac"), c("Kd"), c("Qh"), c("Js"), c("9c"), c("7d"), c("2h")});

    EXPECT_GT(sf,    quads);
    EXPECT_GT(quads, fh);
    EXPECT_GT(fh,    flush);
    EXPECT_GT(flush, str8);
    EXPECT_GT(str8,  trips);
    EXPECT_GT(trips, twop);
    EXPECT_GT(twop,  onep);
    EXPECT_GT(onep,  hcard);
}

TEST(HandEval, BestFiveFromSeven) {
    // Board: Ah Kh Qh Jh 2c 3d 4h
    // Best 5: Ah Kh Qh Jh 4h = Ace-high flush (not straight flush — J not connecting to T)
    // Actually Ah Kh Qh Jh 4h is a flush; check it beats a straight
    std::array<Card,7> cards = {c("Ah"), c("Kh"), c("Qh"), c("Jh"), c("2c"), c("3d"), c("4h")};
    uint16_t r = eval().evaluate(cards.data(), 7);
    EXPECT_EQ(HandEvaluator::category(r), HandEvaluator::Category::Flush);
}

// ── Category tests ────────────────────────────────────────────────────────

TEST(HandEval, Categories) {
    auto cat7 = [&](std::array<Card,7> cards) {
        return HandEvaluator::category(eval().evaluate(cards.data(), 7));
    };

    EXPECT_EQ(cat7({c("Ac"), c("Kd"), c("Qh"), c("Js"), c("9c"), c("7d"), c("2h")}),
              HandEvaluator::Category::HighCard);
    EXPECT_EQ(cat7({c("Ac"), c("Ad"), c("2h"), c("3s"), c("5c"), c("7d"), c("9h")}),
              HandEvaluator::Category::OnePair);
    EXPECT_EQ(cat7({c("Ac"), c("Ad"), c("Kh"), c("Ks"), c("2c"), c("3d"), c("4h")}),
              HandEvaluator::Category::TwoPair);
    // A-2-3-4-5 is the wheel (a straight), so use a non-straight trio
    EXPECT_EQ(cat7({c("Ac"), c("Ad"), c("Ah"), c("2s"), c("7c"), c("9d"), c("Jh")}),
              HandEvaluator::Category::ThreeOfAKind);
    EXPECT_EQ(cat7({c("9c"), c("8d"), c("7h"), c("6s"), c("5c"), c("2h"), c("3d")}),
              HandEvaluator::Category::Straight);
    EXPECT_EQ(cat7({c("Ac"), c("Kc"), c("Tc"), c("6c"), c("2c"), c("3d"), c("4h")}),
              HandEvaluator::Category::Flush);
    EXPECT_EQ(cat7({c("Ac"), c("Ad"), c("Ah"), c("Ks"), c("Kh"), c("2c"), c("3d")}),
              HandEvaluator::Category::FullHouse);
    EXPECT_EQ(cat7({c("Ac"), c("Ad"), c("Ah"), c("As"), c("Kh"), c("2c"), c("3d")}),
              HandEvaluator::Category::FourOfAKind);
    EXPECT_EQ(cat7({c("9h"), c("8h"), c("7h"), c("6h"), c("5h"), c("2c"), c("3d")}),
              HandEvaluator::Category::StraightFlush);
    EXPECT_EQ(cat7({c("Ah"), c("Kh"), c("Qh"), c("Jh"), c("Th"), c("2c"), c("3d")}),
              HandEvaluator::Category::StraightFlush);
}

// ── Full enumeration test ─────────────────────────────────────────────────
// Enumerate all C(52,7) = 133,784,560 hands and verify known category
// frequency counts from combinatorics tables.
//
// Known 7-card frequencies (Poker hands out of 133,784,560):
//   Straight flush   :     41,584
//   Four of a kind   :    224,848
//   Full house       :  3,473,184
//   Flush            :  4,047,644
//   Straight         :  6,180,020
//   Three of a kind  :  6,461,620
//   Two pair         : 31,433,400
//   One pair         : 58,627,800
//   High card        : 23,294,460
//
// This test takes ~10–30 seconds in release mode; disable in Debug with
// the SLOW_TESTS env variable unset if needed.

TEST(HandEval, FullEnumeration) {
    if (std::getenv("SLOW_TESTS") == nullptr) {
        GTEST_SKIP() << "Set SLOW_TESTS=1 to run full enumeration (~30s)";
    }

    const long long kExpected[] = {
        23294460LL,   // HighCard
        58627800LL,   // OnePair
        31433400LL,   // TwoPair
         6461620LL,   // ThreeOfAKind
         6180020LL,   // Straight
         4047644LL,   // Flush
         3473184LL,   // FullHouse
          224848LL,   // FourOfAKind
           41584LL,   // StraightFlush
    };

    long long counts[9] = {};
    long long total = 0;

    for (int a = 0; a < 52; ++a)
    for (int b = a+1; b < 52; ++b)
    for (int d = b+1; d < 52; ++d)
    for (int e = d+1; e < 52; ++e)
    for (int f = e+1; f < 52; ++f)
    for (int g = f+1; g < 52; ++g)
    for (int h = g+1; h < 52; ++h) {
        Card cards[7] = {Card(a), Card(b), Card(d), Card(e), Card(f), Card(g), Card(h)};
        uint16_t r = eval().evaluate(cards, 7);
        counts[static_cast<int>(HandEvaluator::category(r))]++;
        total++;
    }

    EXPECT_EQ(total, 133784560LL);
    for (int i = 0; i < 9; ++i) {
        EXPECT_EQ(counts[i], kExpected[i])
            << "Category " << i << " mismatch";
    }
}
