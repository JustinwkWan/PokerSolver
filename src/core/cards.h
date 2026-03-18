#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>

// ── Card encoding ──────────────────────────────────────────────────────────
// Card is a uint8_t in [0, 51].
//   rank = card / 4   (0=2, 1=3, ..., 8=T, 9=J, 10=Q, 11=K, 12=A)
//   suit = card % 4   (0=clubs, 1=diamonds, 2=hearts, 3=spades)
using Card = uint8_t;

constexpr Card  kNoCard    = 255;
constexpr int   kNumCards  = 52;
constexpr int   kNumRanks  = 13;
constexpr int   kNumSuits  = 4;

inline int  rankOf(Card c)               { return c >> 2; }
inline int  suitOf(Card c)               { return c & 3; }
inline Card makeCard(int rank, int suit) { return Card((rank << 2) | suit); }

// ── String conversion ──────────────────────────────────────────────────────
// Rank chars: "23456789TJQKA"
// Suit chars: "cdhs"
char        rankChar(int rank);      // '2'..'A'
char        suitChar(int suit);      // 'c','d','h','s'
std::string cardToStr(Card c);       // e.g. "Ah", "Tc", "2d"
Card        cardFromStr(std::string_view s); // "Ah" -> card

// ── Deck ───────────────────────────────────────────────────────────────────
class Deck {
public:
    Deck();

    void reset();                        // restore all 52 cards in order
    void shuffle(std::mt19937_64& rng);  // Fisher-Yates in place
    Card deal();                         // pop from top; UB if empty
    int  remaining() const { return remaining_; }

private:
    std::array<Card, kNumCards> cards_;
    int remaining_;
};

// ── Canonical preflop hand (0–168) ────────────────────────────────────────
// Maps any two-card combination to one of 169 distinct hand types.
//
// Index layout:
//   0–12   : pairs    (AA=0, KK=1, ..., 22=12)
//   13–90  : suited   (AKs=13, AQs=14, ..., 32s=90)
//   91–168 : offsuit  (AKo=91, AQo=92, ..., 32o=168)
//
// Within suited/offsuit groups, ordered by (high rank desc, low rank desc).
int canonicalPreflopHand(Card c1, Card c2);

// Pre-built lookup table [c1][c2] → canonical index (c1 != c2).
// Initialised once by initCards(); call before using the table directly.
extern int gCanonicalHandTable[52][52];
void       initCards(); // idempotent; call once at program start
