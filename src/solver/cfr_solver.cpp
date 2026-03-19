#include "solver/cfr_solver.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <thread>

// ── Construction ─────────────────────────────────────────────────────────

CFRSolver::CFRSolver(const GameTree& tree,
                     const InfoSetManager& info_sets,
                     RegretStore& store,
                     BucketFunc bucket_func,
                     DCFRParams params,
                     uint64_t seed)
    : tree_(&tree),
      info_sets_(&info_sets),
      store_(&store),
      bucket_func_(std::move(bucket_func)),
      params_(params),
      num_players_(tree.numPlayers()),
      rng_(seed) {
    board_.fill(kNoCard);
}

// ── Run (single-threaded) ───────────────────────────────────────────────

void CFRSolver::run(uint64_t num_iterations) {
    for (uint64_t i = 0; i < num_iterations; ++i) {
        // Alternate traversing player each iteration.
        int traverser = static_cast<int>(iteration_ % num_players_);
        runIteration(traverser);
    }
}

void CFRSolver::runIteration(int traverser) {
    sampleCards();
    traverse(0, traverser);  // start from root
    applyDiscounting();
    ++iteration_;
}

// ── Run (multi-threaded) ────────────────────────────────────────────────

void CFRSolver::runParallel(uint64_t num_iterations, int num_threads,
                            uint64_t merge_interval) {
    if (num_threads <= 1) {
        run(num_iterations);
        return;
    }

    // Distribute iterations evenly across threads.
    uint64_t base_per_thread = num_iterations / num_threads;
    uint64_t remainder = num_iterations % num_threads;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        uint64_t iters = base_per_thread + (static_cast<uint64_t>(t) < remainder ? 1 : 0);
        // Each thread gets a unique seed derived from the base seed + thread id.
        uint64_t seed = rng_() + t;
        threads.emplace_back(&CFRSolver::threadWorker, this,
                             t, iters, merge_interval, seed);
    }

    for (auto& thr : threads) {
        thr.join();
    }

    // Apply discounting for the full batch.
    applyDiscounting(num_iterations);
    iteration_ += num_iterations;
}

// ── Thread worker ───────────────────────────────────────────────────────

void CFRSolver::threadWorker(int thread_id, uint64_t iters_per_thread,
                             uint64_t merge_interval, uint64_t base_seed) {
    // Initialize per-thread state.
    ThreadState ts;
    ts.rng.seed(base_seed);
    uint64_t total_entries = store_->totalEntries();
    for (int p = 0; p < num_players_; ++p) {
        ts.local_regrets[p].assign(total_entries, 0.0f);
        ts.local_strategy[p].assign(total_entries, 0.0f);
    }
    ts.local_iterations = 0;

    for (uint64_t i = 0; i < iters_per_thread; ++i) {
        // Alternate traversing player.
        int traverser = static_cast<int>(
            (iteration_ + i * num_players_ + thread_id) % num_players_);

        sampleCards(ts);
        traverse(0, traverser, ts);
        ++ts.local_iterations;

        // Periodically merge into global store.
        if (ts.local_iterations % merge_interval == 0) {
            mergeThreadState(ts);
        }
    }

    // Final merge of any remaining local accumulations.
    if (ts.local_iterations % merge_interval != 0) {
        mergeThreadState(ts);
    }
}

// ── Merge thread-local buffers into global store ────────────────────────

void CFRSolver::mergeThreadState(ThreadState& ts) {
    std::lock_guard<std::mutex> lock(merge_mutex_);

    uint64_t total = store_->totalEntries();
    for (int p = 0; p < num_players_; ++p) {
        float* global_reg = store_->regrets(p);
        float* global_strat = store_->strategy(p);
        float* local_reg = ts.local_regrets[p].data();
        float* local_strat = ts.local_strategy[p].data();

        for (uint64_t i = 0; i < total; ++i) {
            global_reg[i] += local_reg[i];
            global_strat[i] += local_strat[i];
        }

        // Zero out local buffers after merge.
        std::memset(local_reg, 0, total * sizeof(float));
        std::memset(local_strat, 0, total * sizeof(float));
    }
}

// ── Card sampling ───────────────────────────────────────────────────────

void CFRSolver::sampleCards() {
    Deck deck;
    deck.shuffle(rng_);

    // Deal hole cards for all players.
    for (int p = 0; p < num_players_; ++p) {
        hole_[p][0] = deck.deal();
        hole_[p][1] = deck.deal();
    }
    // Deal 5 board cards.
    for (int i = 0; i < 5; ++i)
        board_[i] = deck.deal();

    // Precompute bucket IDs for all (player, street) combos.
    for (int p = 0; p < num_players_; ++p) {
        bucket_ids_[p][0] = bucket_func_(p, 0, hole_[p].data(),
                                          board_.data(), 0);
        bucket_ids_[p][1] = bucket_func_(p, 1, hole_[p].data(),
                                          board_.data(), 3);
        bucket_ids_[p][2] = bucket_func_(p, 2, hole_[p].data(),
                                          board_.data(), 4);
        bucket_ids_[p][3] = bucket_func_(p, 3, hole_[p].data(),
                                          board_.data(), 5);
    }
}

void CFRSolver::sampleCards(ThreadState& ts) {
    Deck deck;
    deck.shuffle(ts.rng);

    for (int p = 0; p < num_players_; ++p) {
        ts.hole[p][0] = deck.deal();
        ts.hole[p][1] = deck.deal();
    }
    for (int i = 0; i < 5; ++i)
        ts.board[i] = deck.deal();

    for (int p = 0; p < num_players_; ++p) {
        ts.bucket_ids[p][0] = bucket_func_(p, 0, ts.hole[p].data(),
                                            ts.board.data(), 0);
        ts.bucket_ids[p][1] = bucket_func_(p, 1, ts.hole[p].data(),
                                            ts.board.data(), 3);
        ts.bucket_ids[p][2] = bucket_func_(p, 2, ts.hole[p].data(),
                                            ts.board.data(), 4);
        ts.bucket_ids[p][3] = bucket_func_(p, 3, ts.hole[p].data(),
                                            ts.board.data(), 5);
    }
}

// ── Tree traversal (single-threaded, uses member state) ─────────────────

float CFRSolver::traverse(uint32_t node_idx, int traverser) {
    const auto& n = tree_->node(node_idx);

    // Terminal node: return payoff.
    if (n.type == NodeType::Terminal) {
        return terminalPayoff(node_idx, traverser);
    }

    int player = n.player;
    int street = n.street;
    int num_actions = n.num_actions;

    // Get bucket for the acting player on this street.
    int bucket = bucket_ids_[player][street];
    uint64_t info_id = info_sets_->infoSetId(node_idx, bucket);

    // Get current strategy from regret matching.
    float strategy[8];  // max 8 actions per node
    assert(num_actions <= 8);
    store_->getStrategy(player, info_id, num_actions, strategy);

    if (player == traverser) {
        // ── Traverser's node: explore ALL actions, compute regrets ────────
        float action_values[8];
        float node_value = 0.0f;

        for (int a = 0; a < num_actions; ++a) {
            uint32_t child_idx = tree_->child(node_idx, a);
            action_values[a] = traverse(child_idx, traverser);
            node_value += strategy[a] * action_values[a];
        }

        // Update cumulative regrets.
        float* regrets = store_->regretSlice(player, info_id);
        for (int a = 0; a < num_actions; ++a) {
            regrets[a] += action_values[a] - node_value;
        }

        return node_value;

    } else {
        // ── Non-traverser's node: sample ONE action according to strategy ─
        float r = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_);
        float cum = 0.0f;
        int sampled = num_actions - 1;  // default to last action
        for (int a = 0; a < num_actions; ++a) {
            cum += strategy[a];
            if (r < cum) {
                sampled = a;
                break;
            }
        }

        // Update cumulative strategy sums (for average strategy computation).
        float* strat_sums = store_->strategySlice(player, info_id);
        for (int a = 0; a < num_actions; ++a) {
            strat_sums[a] += strategy[a];
        }

        uint32_t child_idx = tree_->child(node_idx, sampled);
        return traverse(child_idx, traverser);
    }
}

// ── Tree traversal (multi-threaded, uses ThreadState + local buffers) ───

float CFRSolver::traverse(uint32_t node_idx, int traverser, ThreadState& ts) {
    const auto& n = tree_->node(node_idx);

    if (n.type == NodeType::Terminal) {
        return terminalPayoff(node_idx, traverser, ts);
    }

    int player = n.player;
    int street = n.street;
    int num_actions = n.num_actions;

    int bucket = ts.bucket_ids[player][street];
    uint64_t info_id = info_sets_->infoSetId(node_idx, bucket);

    // Read strategy from global store (read-only, safe without lock —
    // small races on regret values are acceptable in parallel CFR).
    float strategy[8];
    assert(num_actions <= 8);
    store_->getStrategy(player, info_id, num_actions, strategy);

    if (player == traverser) {
        float action_values[8];
        float node_value = 0.0f;

        for (int a = 0; a < num_actions; ++a) {
            uint32_t child_idx = tree_->child(node_idx, a);
            action_values[a] = traverse(child_idx, traverser, ts);
            node_value += strategy[a] * action_values[a];
        }

        // Write regret updates to thread-local buffer.
        uint64_t base = info_id * store_->maxActions();
        for (int a = 0; a < num_actions; ++a) {
            ts.local_regrets[player][base + a] += action_values[a] - node_value;
        }

        return node_value;

    } else {
        float r = std::uniform_real_distribution<float>(0.0f, 1.0f)(ts.rng);
        float cum = 0.0f;
        int sampled = num_actions - 1;
        for (int a = 0; a < num_actions; ++a) {
            cum += strategy[a];
            if (r < cum) {
                sampled = a;
                break;
            }
        }

        // Write strategy sum updates to thread-local buffer.
        uint64_t base = info_id * store_->maxActions();
        for (int a = 0; a < num_actions; ++a) {
            ts.local_strategy[player][base + a] += strategy[a];
        }

        uint32_t child_idx = tree_->child(node_idx, sampled);
        return traverse(child_idx, traverser, ts);
    }
}

// ── Terminal payoff (single-threaded) ───────────────────────────────────

float CFRSolver::terminalPayoff(uint32_t node_idx, int traverser) const {
    const auto& n = tree_->node(node_idx);
    assert(n.type == NodeType::Terminal);

    int pot = n.pot;

    if (n.fold_player >= 0) {
        // Someone folded. In multi-way, the fold_player field stores who folded
        // last, but terminal is reached when only 1 player remains.
        // The non-folding player wins the pot.
        // For N-player: pot was collected, traverser's share = pot - contribution
        // For simplicity, use the same half-pot logic for HU,
        // and for multi-way fold: the winner takes the pot.
        int folder = n.fold_player;
        int winner = -1;
        // In an N-player tree, fold terminal means only 1 non-folded player
        // The winner is determined by the tree's terminal state.
        // For HU: winner = 1 - folder
        if (num_players_ == 2) {
            winner = 1 - folder;
            int half_pot = pot / 2;
            return traverser == winner ? static_cast<float>(half_pot)
                                       : static_cast<float>(-half_pot);
        }

        // For multi-way fold terminals, payoff = pot - my_contribution
        // Each player contributed pot/num_remaining_at_start approx.
        // Since the tree stores the total pot at terminal, and we use
        // equal starting stacks, each player's contribution is proportional.
        // For v1 (no side pots): traverser wins pot if they are the winner,
        // loses their contribution otherwise.
        // The winner is the last non-folded player. We don't track individual
        // contributions in the tree node, so we approximate:
        // payoff = (pot - contribution_from_traverser) if winner
        //        = (-contribution_from_traverser) if loser
        // Since we don't know exact contributions from the tree alone,
        // use the standard approach: traverser payoff = winnings - stack_start_contribution
        // For fold terminals in the abstract tree during external sampling,
        // we use: winner gets +pot/num_players*(num_players-1),
        //         loser gets -pot/num_players
        // This approximation works because external sampling already weights by probability.
        int share = pot / num_players_;
        if (traverser == (1 - folder)) {
            // Traverser is winner (only valid for last fold in 2-remaining situations)
            return static_cast<float>(pot - share);
        }
        return static_cast<float>(-share);
    }

    // Showdown: evaluate all non-folded hands, best hand wins.
    // For external sampling, all players have hole cards.
    uint16_t ranks[kMaxPlayers];
    uint16_t best_rank = 0;
    for (int p = 0; p < num_players_; ++p) {
        ranks[p] = eval_.evaluate(hole_[p], board_);
        if (ranks[p] > best_rank) best_rank = ranks[p];
    }

    // Count winners (for split pots).
    int num_winners = 0;
    for (int p = 0; p < num_players_; ++p) {
        if (ranks[p] == best_rank) ++num_winners;
    }

    // Traverser's payoff.
    int share = pot / num_players_;  // each player's contribution
    if (ranks[traverser] == best_rank) {
        // Winner or split: wins pot / num_winners
        float winnings = static_cast<float>(pot) / num_winners;
        return winnings - static_cast<float>(share);
    } else {
        return static_cast<float>(-share);
    }
}

// ── Terminal payoff (multi-threaded, uses ThreadState) ──────────────────

float CFRSolver::terminalPayoff(uint32_t node_idx, int traverser,
                                const ThreadState& ts) const {
    const auto& n = tree_->node(node_idx);
    assert(n.type == NodeType::Terminal);

    int pot = n.pot;

    if (n.fold_player >= 0) {
        if (num_players_ == 2) {
            int winner = 1 - n.fold_player;
            int half_pot = pot / 2;
            return traverser == winner ? static_cast<float>(half_pot)
                                       : static_cast<float>(-half_pot);
        }

        int share = pot / num_players_;
        if (traverser == (1 - n.fold_player)) {
            return static_cast<float>(pot - share);
        }
        return static_cast<float>(-share);
    }

    // Showdown.
    uint16_t ranks[kMaxPlayers];
    uint16_t best_rank = 0;
    for (int p = 0; p < num_players_; ++p) {
        ranks[p] = ts.eval.evaluate(ts.hole[p], ts.board);
        if (ranks[p] > best_rank) best_rank = ranks[p];
    }

    int num_winners = 0;
    for (int p = 0; p < num_players_; ++p) {
        if (ranks[p] == best_rank) ++num_winners;
    }

    int share = pot / num_players_;
    if (ranks[traverser] == best_rank) {
        float winnings = static_cast<float>(pot) / num_winners;
        return winnings - static_cast<float>(share);
    } else {
        return static_cast<float>(-share);
    }
}

// ── DCFR discounting (single-threaded, per-iteration) ───────────────────

void CFRSolver::applyDiscounting() {
    uint64_t t = iteration_ + 1;  // 1-indexed

    float t_f = static_cast<float>(t);
    float t_alpha = std::pow(t_f, params_.alpha);
    float t_beta  = std::pow(t_f, params_.beta);

    float pos_weight = t_alpha / (t_alpha + 1.0f);
    float neg_weight = t_beta  / (t_beta  + 1.0f);
    float strat_weight = std::pow(t_f / (t_f + 1.0f), params_.gamma);

    uint64_t total = store_->totalEntries();

    for (int p = 0; p < num_players_; ++p) {
        float* reg = store_->regrets(p);
        float* strat = store_->strategy(p);

        for (uint64_t i = 0; i < total; ++i) {
            if (reg[i] > 0.0f) {
                reg[i] *= pos_weight;
            } else {
                reg[i] *= neg_weight;
            }
            strat[i] *= strat_weight;
        }
    }
}

// ── DCFR discounting (batch, after parallel merge) ──────────────────────

void CFRSolver::applyDiscounting(uint64_t num_iterations_in_batch) {
    uint64_t t_start = iteration_ + 1;
    uint64_t t_end = iteration_ + num_iterations_in_batch;
    uint64_t t_mid = (t_start + t_end) / 2;

    float t_f = static_cast<float>(t_mid);
    float t_alpha = std::pow(t_f, params_.alpha);
    float t_beta  = std::pow(t_f, params_.beta);

    float n = static_cast<float>(num_iterations_in_batch);
    float pos_weight = std::pow(t_alpha / (t_alpha + 1.0f), n);
    float neg_weight = std::pow(t_beta  / (t_beta  + 1.0f), n);
    float strat_weight = std::pow(
        std::pow(t_f / (t_f + 1.0f), params_.gamma), n);

    uint64_t total = store_->totalEntries();

    for (int p = 0; p < num_players_; ++p) {
        float* reg = store_->regrets(p);
        float* strat = store_->strategy(p);

        for (uint64_t i = 0; i < total; ++i) {
            if (reg[i] > 0.0f) {
                reg[i] *= pos_weight;
            } else {
                reg[i] *= neg_weight;
            }
            strat[i] *= strat_weight;
        }
    }
}
