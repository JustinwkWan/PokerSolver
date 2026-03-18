#pragma once

#include "core/cards.h"

#include <array>
#include <cstdint>
#include <vector>

// ── SuitIsomorphism ──────────────────────────────────────────────────────
// Detects which suits are strategically interchangeable given a board.
//
// Two suits are isomorphic if they appear the same number of times on the
// board. Swapping isomorphic suits across all cards (board + hole cards)
// produces a strategically equivalent game state because:
//   - Hand evaluation ranks are suit-agnostic (only flush/straight-flush care)
//   - Flush draw potential depends only on suit frequency on the board
//
// This enables 4-6x tree reduction at chance nodes with zero accuracy loss.

struct SuitMapping {
    // For each suit (0-3), its equivalence class ID (0-based).
    std::array<int, 4> suit_class;

    // For each equivalence class, the canonical (representative) suit.
    // Indexed by class ID. Only entries [0..num_classes) are valid.
    std::array<int, 4> canonical_suit;

    // How many suits in each class. Indexed by class ID.
    std::array<int, 4> class_size;

    // Number of distinct equivalence classes.
    int num_classes;
};

class IsomorphismDetector {
public:
    // Compute suit isomorphism classes for a given board.
    // board_cards: array of cards on the board (0-5 cards).
    // num_board: number of board cards (0 = preflop, 3 = flop, 4 = turn, 5 = river).
    static SuitMapping compute(const Card* board_cards, int num_board);

    // Map a card to its canonical form under the given isomorphism.
    // Replaces the card's suit with the canonical suit for its class.
    static Card canonicalize(Card card, const SuitMapping& mapping);

    // Number of unique cards to consider at a chance node.
    // Instead of branching on all 52 cards, branch on cards with canonical suits.
    // Returns the set of canonical cards not blocked by dead cards.
    static std::vector<Card> canonicalNextCards(
        const SuitMapping& mapping, uint64_t dead_mask);

    // Get the multiplicity of a canonical card — how many actual cards it
    // represents (i.e., class_size of its suit class).
    static int multiplicity(Card canonical_card, const SuitMapping& mapping);

    // Generate all suit permutations that map canonical → actual.
    // Each permutation is a suit_map[4] where suit_map[canonical_suit] = actual_suit.
    // Used by the solver to copy strategies to isomorphic equivalents.
    static std::vector<std::array<int, 4>> expansionPermutations(
        const SuitMapping& mapping);
};
