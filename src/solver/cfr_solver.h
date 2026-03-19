#pragma once

#include "core/cards.h"
#include "core/game_state.h"
#include "core/hand_evaluator.h"
#include "solver/regret_store.h"
#include "tree/game_tree.h"
#include "tree/info_set.h"

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <random>
#include <vector>

// ── DCFR parameters ──────────────────────────────────────────────────────

struct DCFRParams {
    float alpha = 1.5f;   // positive regret discount
    float beta  = 0.0f;   // negative regret discount
    float gamma = 2.0f;   // strategy contribution discount
};

// ── Per-thread state ─────────────────────────────────────────────────────
// Each thread gets its own RNG, sampled cards, bucket IDs, hand evaluator,
// and local accumulation buffers for regrets and strategy sums.

struct ThreadState {
    std::mt19937_64 rng;
    HandEvaluator eval;

    std::array<std::array<Card, 2>, kMaxPlayers> hole;  // hole[player][card]
    std::array<Card, 5> board;                           // full 5-card board
    std::array<std::array<int, 4>, kMaxPlayers> bucket_ids;  // bucket_ids[player][street]

    // Thread-local accumulation buffers (same layout as RegretStore).
    std::vector<float> local_regrets[kMaxPlayers];
    std::vector<float> local_strategy[kMaxPlayers];
    uint64_t local_iterations = 0;

    ThreadState() { board.fill(kNoCard); }
};

// ── CFRSolver ────────────────────────────────────────────────────────────
// Discounted Counterfactual Regret Minimization with external sampling.
//
// Each iteration:
//   1. Sample cards: hole cards for all players + 5 board cards.
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

    // Run n iterations of external sampling DCFR (single-threaded).
    // Alternates traversing player each iteration.
    void run(uint64_t num_iterations);

    // Run n iterations across multiple threads.
    // Each thread accumulates regrets/strategy locally and merges into the
    // global RegretStore every merge_interval iterations.
    void runParallel(uint64_t num_iterations, int num_threads,
                     uint64_t merge_interval = 1000);

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
    int num_players_;

    HandEvaluator eval_;
    std::mt19937_64 rng_;
    uint64_t iteration_ = 0;

    // Sampled cards for current iteration (used by single-threaded path).
    std::array<std::array<Card, 2>, kMaxPlayers> hole_;
    std::array<Card, 5> board_;

    // Precomputed bucket IDs for current iteration: bucket_ids_[player][street].
    std::array<std::array<int, 4>, kMaxPlayers> bucket_ids_;

    // Mutex for merging thread-local buffers into the global store.
    std::mutex merge_mutex_;

    // ── Sampling ─────────────────────────────────────────────────────────

    void sampleCards();
    void sampleCards(ThreadState& ts);

    // ── Tree traversal ───────────────────────────────────────────────────

    // Single-threaded traversal (uses member state).
    float traverse(uint32_t node_idx, int traverser);

    // Thread-safe traversal (uses ThreadState, writes to local buffers).
    float traverse(uint32_t node_idx, int traverser, ThreadState& ts);

    // Terminal node payoff for the traversing player.
    float terminalPayoff(uint32_t node_idx, int traverser) const;
    float terminalPayoff(uint32_t node_idx, int traverser,
                         const ThreadState& ts) const;

    // ── Parallel helpers ─────────────────────────────────────────────────

    // Thread worker function: runs iterations and merges periodically.
    void threadWorker(int thread_id, uint64_t iters_per_thread,
                      uint64_t merge_interval, uint64_t base_seed);

    // Merge a thread's local buffers into the global RegretStore.
    void mergeThreadState(ThreadState& ts);

    // ── DCFR discounting ─────────────────────────────────────────────────

    void applyDiscounting();
    void applyDiscounting(uint64_t num_iterations_in_batch);
};
