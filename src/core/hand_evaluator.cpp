#include "core/hand_evaluator.h"

#include "omp/HandEvaluator.h"

#include <cassert>
#include <stdexcept>

// ── OMPEval card conversion ────────────────────────────────────────────────
// OMPEval uses CARD = 4 * RANK + SUIT — identical to our encoding.
// No conversion needed; pass card values directly.
static inline unsigned toOmpCard(Card c) {
    return static_cast<unsigned>(c);
}

// ── Pimpl ─────────────────────────────────────────────────────────────────
struct HandEvaluator::Impl {
    omp::HandEvaluator eval;  // construction initialises ~200 KB lookup tables
};

HandEvaluator::HandEvaluator() : impl_(new Impl{}) {}

// ── evaluate (hole + full board) ───────────────────────────────────────────
uint16_t HandEvaluator::evaluate(const std::array<Card, 2>& hole,
                                  const std::array<Card, 5>& board) const {
    omp::Hand h = omp::Hand::empty();
    h += omp::Hand(toOmpCard(hole[0]));
    h += omp::Hand(toOmpCard(hole[1]));
    for (Card c : board) h += omp::Hand(toOmpCard(c));
    return impl_->eval.evaluate(h);
}

// ── evaluate (arbitrary n cards, 5 ≤ n ≤ 7) ──────────────────────────────
uint16_t HandEvaluator::evaluate(const Card* cards, int n) const {
    assert(n >= 5 && n <= 7);
    omp::Hand h = omp::Hand::empty();
    for (int i = 0; i < n; ++i) h += omp::Hand(toOmpCard(cards[i]));
    return impl_->eval.evaluate(h);
}

// ── Hand category ──────────────────────────────────────────────────────────
// OMPEval encodes hand category directly in the rank value:
//   rank / 4096 == 1  →  High card
//   rank / 4096 == 2  →  One pair
//   rank / 4096 == 3  →  Two pair
//   rank / 4096 == 4  →  Three of a kind
//   rank / 4096 == 5  →  Straight
//   rank / 4096 == 6  →  Flush
//   rank / 4096 == 7  →  Full house
//   rank / 4096 == 8  →  Four of a kind
//   rank / 4096 == 9  →  Straight flush (incl. royal flush)
HandEvaluator::Category HandEvaluator::category(uint16_t rank) {
    int cat = rank / 4096;
    if (cat < 1) cat = 1;   // shouldn't happen for complete 5-7 card hands
    if (cat > 9) cat = 9;
    return static_cast<Category>(cat - 1);  // shift to 0-indexed enum
}

const char* HandEvaluator::categoryName(Category c) {
    switch (c) {
        case Category::HighCard:      return "High Card";
        case Category::OnePair:       return "One Pair";
        case Category::TwoPair:       return "Two Pair";
        case Category::ThreeOfAKind:  return "Three of a Kind";
        case Category::Straight:      return "Straight";
        case Category::Flush:         return "Flush";
        case Category::FullHouse:     return "Full House";
        case Category::FourOfAKind:   return "Four of a Kind";
        case Category::StraightFlush: return "Straight Flush";
    }
    return "Unknown";
}
