#include "core/cards.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <stdexcept>

// ── String conversion ──────────────────────────────────────────────────────

char rankChar(int rank) {
    constexpr char kRankChars[] = "23456789TJQKA";
    assert(rank >= 0 && rank < kNumRanks);
    return kRankChars[rank];
}

char suitChar(int suit) {
    constexpr char kSuitChars[] = "cdhs";
    assert(suit >= 0 && suit < kNumSuits);
    return kSuitChars[suit];
}

std::string cardToStr(Card c) {
    assert(c < kNumCards);
    return {rankChar(rankOf(c)), suitChar(suitOf(c))};
}

Card cardFromStr(std::string_view s) {
    if (s.size() < 2) throw std::invalid_argument("bad card string");

    constexpr std::string_view kRankChars = "23456789TJQKA";
    constexpr std::string_view kSuitChars = "cdhs";

    char r = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    char su = static_cast<char>(std::tolower(static_cast<unsigned char>(s[1])));

    auto ri = kRankChars.find(r);
    auto si = kSuitChars.find(su);
    if (ri == std::string_view::npos || si == std::string_view::npos)
        throw std::invalid_argument(std::string("bad card string: ") + std::string(s));

    return makeCard(static_cast<int>(ri), static_cast<int>(si));
}

// ── Deck ───────────────────────────────────────────────────────────────────

Deck::Deck() { reset(); }

void Deck::reset() {
    for (int i = 0; i < kNumCards; ++i) cards_[i] = Card(i);
    remaining_ = kNumCards;
}

void Deck::shuffle(std::mt19937_64& rng) {
    for (int i = remaining_ - 1; i > 0; --i) {
        std::uniform_int_distribution<int> dist(0, i);
        int j = dist(rng);
        std::swap(cards_[i], cards_[j]);
    }
}

Card Deck::deal() {
    assert(remaining_ > 0);
    return cards_[--remaining_];
}

// ── Canonical preflop hand ─────────────────────────────────────────────────
//
// For a hand (high_rank h, low_rank l, h > l):
//   suited_offset(h, l) = 78 - h*(h+1)/2 + (h - 1 - l)
//
// Verification:
//   AK suited: h=12, l=11 → 78 - 78 + 0 = 0  → canonical 13
//   A2 suited: h=12, l=0  → 78 - 78 + 11 = 11 → canonical 24
//   KQ suited: h=11, l=10 → 78 - 66 + 0 = 12  → canonical 25
//   32 suited: h=1,  l=0  → 78 - 1  + 0 = 77  → canonical 90

static int suitedOffset(int h, int l) {
    assert(h > l && h < kNumRanks && l >= 0);
    return 78 - h * (h + 1) / 2 + (h - 1 - l);
}

int canonicalPreflopHand(Card c1, Card c2) {
    assert(c1 != c2 && c1 < kNumCards && c2 < kNumCards);
    int r1 = rankOf(c1), r2 = rankOf(c2);
    int s1 = suitOf(c1), s2 = suitOf(c2);

    if (r1 == r2) {
        // Pair: AA=0, KK=1, ..., 22=12
        return 12 - r1;
    }

    int h = std::max(r1, r2);
    int l = std::min(r1, r2);
    bool suited = (s1 == s2);
    int offset = suitedOffset(h, l);

    return suited ? (13 + offset) : (91 + offset);
}

// ── Lookup table ───────────────────────────────────────────────────────────

int gCanonicalHandTable[52][52];

static bool gCardsInitialised = false;

void initCards() {
    if (gCardsInitialised) return;
    gCardsInitialised = true;

    for (int i = 0; i < kNumCards; ++i)
        for (int j = 0; j < kNumCards; ++j)
            if (i != j)
                gCanonicalHandTable[i][j] = canonicalPreflopHand(Card(i), Card(j));
}
