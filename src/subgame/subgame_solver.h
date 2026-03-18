#pragma once

#include "core/cards.h"
#include "core/hand_evaluator.h"
#include "solver/cfr_solver.h"
#include "solver/regret_store.h"
#include "tree/game_tree.h"
#include "tree/info_set.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

// ── SubgameSolver ────────────────────────────────────────────────────────
// Re-solves a subgame from a specific game state with finer granularity.
//
// "Unsafe" nested subgame solving:
//   1. Build a subtree from the given game state using a (potentially finer)
//      action abstraction.
//   2. Run DCFR on the subtree.
//   3. Return the refined strategy for the root of the subtree.
//
// The subgame is rooted at a specific (street, pot, stacks, action history)
// and uses the same bucket function for card abstraction.
//
// Leaf values come from the blueprint when the subgame tree reaches states
// that exist in the full tree. For terminal nodes (fold/showdown), values
// are computed directly.

struct SubgameConfig {
    int num_iterations = 1000;       // DCFR iterations on the subgame
    DCFRParams dcfr_params = {};     // DCFR discount parameters
    uint64_t seed = 42;
};

struct SubgameResult {
    // The refined strategy at the root for each action.
    std::vector<float> strategy;     // probabilities for each action
    std::vector<Action> actions;     // the actions available

    // Subgame statistics.
    uint32_t num_nodes;
    uint64_t num_info_sets;
    int num_iterations;
};

class SubgameSolver {
public:
    // Solve a subgame rooted at the given game state.
    //
    // state:           the game state at the root of the subgame
    // aa:              action abstraction for the subgame (can be finer than blueprint)
    // buckets:         bucket counts per street [preflop, flop, turn, river]
    // bucket_func:     card-to-bucket mapping function
    // hole:            hole cards for both players (for the specific deal)
    // board:           board cards (up to 5; only num_board_cards are valid)
    // config:          solver configuration
    static SubgameResult solve(const GameState& state,
                                const ActionAbstraction& aa,
                                const std::array<int, 4>& buckets,
                                const CFRSolver::BucketFunc& bucket_func,
                                const std::array<std::array<Card, 2>, 2>& hole,
                                const std::array<Card, 5>& board,
                                const SubgameConfig& config = {});
};
