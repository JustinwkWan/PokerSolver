#pragma once

#include "core/cards.h"
#include <array>
#include <cstdint>

// ── HandEvaluator ──────────────────────────────────────────────────────────
// Thin wrapper around OMPEval.
//
// evaluate() returns a uint16_t where HIGHER = BETTER.
// The value is opaque — use only for comparison and category queries.
//
// OMPEval card index: rank + suit * 13
// Our card index:     rank * 4 + suit
// Conversion: ompCard(c) = rankOf(c) + suitOf(c) * 13

class HandEvaluator {
public:
    HandEvaluator();  // initialises OMPEval lookup tables (one-time cost ~200 KB)

    // Evaluate 2 hole cards + 5 board cards (standard 7-card eval).
    uint16_t evaluate(const std::array<Card, 2>& hole,
                      const std::array<Card, 5>& board) const;

    // Evaluate any 5, 6, or 7 cards given as a plain array.
    uint16_t evaluate(const Card* cards, int n) const;

    // ── Hand categories ───────────────────────────────────────────────────
    enum class Category : uint8_t {
        HighCard = 0,
        OnePair,
        TwoPair,
        ThreeOfAKind,
        Straight,
        Flush,
        FullHouse,
        FourOfAKind,
        StraightFlush   // includes royal flush
    };

    static Category  category(uint16_t rank);
    static const char* categoryName(Category c);

private:
    // OMPEval evaluator stored as opaque bytes to avoid exposing omp/ headers
    // in this header (keeps compile times fast for consumers).
    struct Impl;
    Impl* impl_;  // owning, never null after construction
};
