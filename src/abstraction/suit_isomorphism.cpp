#include "abstraction/suit_isomorphism.h"

#include <algorithm>
#include <cassert>

// ── compute ──────────────────────────────────────────────────────────────

SuitMapping IsomorphismDetector::compute(const Card* board_cards, int num_board) {
    assert(num_board >= 0 && num_board <= 5);

    // Count how many times each suit appears on the board.
    std::array<int, 4> freq = {0, 0, 0, 0};
    for (int i = 0; i < num_board; ++i)
        freq[suitOf(board_cards[i])]++;

    // Group suits by frequency. Suits with the same frequency are isomorphic.
    SuitMapping mapping;
    mapping.num_classes = 0;

    // Track which frequencies we've seen and their assigned class ID.
    // freq_to_class[f] = class ID for frequency f (-1 if not yet assigned).
    std::array<int, 6> freq_to_class;  // max 5 board cards → max freq = 5
    freq_to_class.fill(-1);

    for (int s = 0; s < 4; ++s) {
        int f = freq[s];
        if (freq_to_class[f] == -1) {
            int cls = mapping.num_classes++;
            freq_to_class[f] = cls;
            mapping.canonical_suit[cls] = s;
            mapping.class_size[cls] = 0;
        }
        int cls = freq_to_class[f];
        mapping.suit_class[s] = cls;
        mapping.class_size[cls]++;
    }

    // Zero out unused class entries.
    for (int i = mapping.num_classes; i < 4; ++i) {
        mapping.canonical_suit[i] = -1;
        mapping.class_size[i] = 0;
    }

    return mapping;
}

// ── canonicalize ─────────────────────────────────────────────────────────

Card IsomorphismDetector::canonicalize(Card card, const SuitMapping& mapping) {
    int rank = rankOf(card);
    int suit = suitOf(card);
    int cls  = mapping.suit_class[suit];
    return makeCard(rank, mapping.canonical_suit[cls]);
}

// ── canonicalNextCards ───────────────────────────────────────────────────

std::vector<Card> IsomorphismDetector::canonicalNextCards(
        const SuitMapping& mapping, uint64_t dead_mask) {
    std::vector<Card> result;
    result.reserve(kNumCards);

    for (int rank = 0; rank < kNumRanks; ++rank) {
        // For each rank, emit one card per suit class (if not dead).
        for (int cls = 0; cls < mapping.num_classes; ++cls) {
            Card c = makeCard(rank, mapping.canonical_suit[cls]);
            if (dead_mask & (uint64_t(1) << c))
                continue;
            result.push_back(c);
        }
    }

    return result;
}

// ── multiplicity ─────────────────────────────────────────────────────────

int IsomorphismDetector::multiplicity(Card canonical_card,
                                       const SuitMapping& mapping) {
    int suit = suitOf(canonical_card);
    int cls  = mapping.suit_class[suit];
    return mapping.class_size[cls];
}

// ── expansionPermutations ────────────────────────────────────────────────

std::vector<std::array<int, 4>> IsomorphismDetector::expansionPermutations(
        const SuitMapping& mapping) {
    // Collect suits per class (already sorted since we iterate s=0..3).
    std::array<std::vector<int>, 4> suits_in_class;
    for (int s = 0; s < 4; ++s)
        suits_in_class[mapping.suit_class[s]].push_back(s);

    // Start with the identity permutation: suit_map[s] = s.
    std::vector<std::array<int, 4>> perms;
    perms.push_back({0, 1, 2, 3});

    // For each class with >1 suit, expand by all permutations of those suits.
    for (int cls = 0; cls < mapping.num_classes; ++cls) {
        const auto& orig = suits_in_class[cls];
        if (orig.size() <= 1) continue;

        auto suits = orig;  // working copy for next_permutation
        std::vector<std::array<int, 4>> expanded;
        expanded.reserve(perms.size() * suits.size());

        // Iterate all permutations of the suits in this class.
        do {
            for (const auto& base : perms) {
                auto p = base;
                // Map: original suit orig[i] → permuted suit suits[i].
                for (int i = 0; i < (int)orig.size(); ++i)
                    p[orig[i]] = suits[i];
                expanded.push_back(p);
            }
        } while (std::next_permutation(suits.begin(), suits.end()));

        perms = std::move(expanded);
    }

    return perms;
}
