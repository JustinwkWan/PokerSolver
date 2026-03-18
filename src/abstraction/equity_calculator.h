#pragma once

#include "core/cards.h"
#include "core/hand_evaluator.h"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

// A 64-bit bitmask where bit i means card i is dead (dealt/known).
using CardMask = uint64_t;

inline CardMask cardBit(Card c) { return uint64_t(1) << c; }

// Build a CardMask from a set of cards.
inline CardMask makeDeadMask(const Card* cards, int n) {
    CardMask mask = 0;
    for (int i = 0; i < n; ++i) mask |= cardBit(cards[i]);
    return mask;
}

// ── EquityCalculator ─────────────────────────────────────────────────────
// Computes hand equity (win probability) against a uniform random opponent.
//
// Two modes:
//   1. River (exact): enumerate all C(remaining,2) opponent holdings.
//   2. Flop/Turn (Monte Carlo): sample random completions + opponent hands.

class EquityCalculator {
public:
    EquityCalculator();

    // ── River equity (exact) ──────────────────────────────────────────────
    // Enumerate all opponent holdings (C(45,2) = 990 combos) and return
    // equity in [0,1]. Ties count as 0.5.
    double riverEquityExact(const std::array<Card, 2>& hole,
                            const std::array<Card, 5>& board) const;

    // ── Batch river equity ────────────────────────────────────────────────
    // Compute equity for ALL valid hole card combos on a given board.
    // Returns one entry per combo that doesn't conflict with the board.
    struct HoleEquity {
        std::array<Card, 2> hole;
        double equity;
    };
    std::vector<HoleEquity> allRiverEquities(
        const std::array<Card, 5>& board) const;

    // ── Monte Carlo equity (flop/turn) ────────────────────────────────────
    // Samples random (opponent, remaining board) combos.
    // board_cards: known board cards (3 for flop, 4 for turn).
    // num_board: 3 or 4.
    double equityMonteCarlo(const std::array<Card, 2>& hole,
                            const Card* board_cards, int num_board,
                            int num_samples,
                            std::mt19937_64& rng) const;

private:
    HandEvaluator eval_;
};
