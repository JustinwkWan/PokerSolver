#pragma once

#include "core/cards.h"
#include "core/hand_evaluator.h"
#include "solver/regret_store.h"
#include "tree/game_tree.h"
#include "tree/info_set.h"

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

// ── DCFR parameters ──────────────────────────────────────────────────────

struct DCFRParams {
    float alpha = 1.5f;   // positive regret discount
    float beta  = 0.0f;   // negative regret discount
    float gamma = 2.0f;   // strategy contribution discount
};

// ── CFRSolver ────────────────────────────────────────────────────────────
// Discounted Counterfactual Regret Minimization with external sampling.
//
// Each iteration:
//   1. Sample cards: hole cards for both players, flop, turn, river.
//   2. Compute bucket IDs for each player on each street.
//   3. Traverse the game tree, computing counterfactual values.
//   4. Update cumulative regrets and strategy sums.
//   5. Apply DCFR discounting every iteration.
//
// The solver operates on the abstract game tree (no card branching).
// Cards are sampled externally and mapped to buckets.

class CFRSolver {
public:
    // bucket_func: given (player, street, hole[2], board[0..4], num_board_cards),
    //              returns the bucket ID for that player on that street.
    using BucketFunc = std::function<int(int player, int street,
                                         const Card hole[2],
                                         const Card board[5],
                                         int num_board_cards)>;

    CFRSolver(const GameTree& tree,
              const InfoSetManager& info_sets,
              RegretStore& store,
              BucketFunc bucket_func,
              DCFRParams params = {},
              uint64_t seed = 42);

    // Run n iterations of external sampling DCFR.
    // Alternates traversing player each iteration.
    void run(uint64_t num_iterations);

    // Run a single iteration for a specific traversing player.
    void runIteration(int traverser);

    // Current iteration count.
    uint64_t iteration() const { return iteration_; }

private:
    const GameTree* tree_;
    const InfoSetManager* info_sets_;
    RegretStore* store_;
    BucketFunc bucket_func_;
    DCFRParams params_;

    HandEvaluator eval_;
    std::mt19937_64 rng_;
    uint64_t iteration_ = 0;

    // Sampled cards for current iteration.
    std::array<std::array<Card, 2>, 2> hole_;  // hole_[player][card]
    std::array<Card, 5> board_;                 // full 5-card board

    // Precomputed bucket IDs for current iteration: bucket_ids_[player][street].
    std::array<std::array<int, 4>, 2> bucket_ids_;

    // ── Sampling ─────────────────────────────────────────────────────────

    void sampleCards();

    // ── Tree traversal ───────────────────────────────────────────────────

    // External sampling CFR traversal. Returns the counterfactual value
    // for the traversing player at this node.
    float traverse(uint32_t node_idx, int traverser);

    // Terminal node payoff for the traversing player.
    float terminalPayoff(uint32_t node_idx, int traverser) const;

    // ── DCFR discounting ─────────────────────────────────────────────────

    void applyDiscounting();
};
