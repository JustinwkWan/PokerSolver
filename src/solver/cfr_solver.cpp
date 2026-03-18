#include "solver/cfr_solver.h"

#include <algorithm>
#include <cassert>
#include <cmath>

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
      rng_(seed) {
    board_.fill(kNoCard);
}

// ── Run ──────────────────────────────────────────────────────────────────

void CFRSolver::run(uint64_t num_iterations) {
    for (uint64_t i = 0; i < num_iterations; ++i) {
        // Alternate traversing player each iteration.
        int traverser = static_cast<int>(iteration_ % 2);
        runIteration(traverser);
    }
}

void CFRSolver::runIteration(int traverser) {
    sampleCards();
    traverse(0, traverser);  // start from root
    applyDiscounting();
    ++iteration_;
}

// ── Card sampling ────────────────────────────────────────────────────────

void CFRSolver::sampleCards() {
    // Build a deck and deal: 2 hole cards per player + 5 board cards = 9 total.
    Deck deck;
    deck.shuffle(rng_);

    hole_[0][0] = deck.deal();
    hole_[0][1] = deck.deal();
    hole_[1][0] = deck.deal();
    hole_[1][1] = deck.deal();
    board_[0]   = deck.deal();
    board_[1]   = deck.deal();
    board_[2]   = deck.deal();
    board_[3]   = deck.deal();
    board_[4]   = deck.deal();

    // Precompute bucket IDs for all (player, street) combos.
    for (int p = 0; p < 2; ++p) {
        // Preflop: 0 board cards.
        bucket_ids_[p][0] = bucket_func_(p, 0, hole_[p].data(),
                                          board_.data(), 0);
        // Flop: 3 board cards.
        bucket_ids_[p][1] = bucket_func_(p, 1, hole_[p].data(),
                                          board_.data(), 3);
        // Turn: 4 board cards.
        bucket_ids_[p][2] = bucket_func_(p, 2, hole_[p].data(),
                                          board_.data(), 4);
        // River: 5 board cards.
        bucket_ids_[p][3] = bucket_func_(p, 3, hole_[p].data(),
                                          board_.data(), 5);
    }
}

// ── Tree traversal ───────────────────────────────────────────────────────

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
        // ── Opponent's node: sample ONE action according to strategy ──────
        // (External sampling: only follow one opponent action.)
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

// ── Terminal payoff ──────────────────────────────────────────────────────

float CFRSolver::terminalPayoff(uint32_t node_idx, int traverser) const {
    const auto& n = tree_->node(node_idx);
    assert(n.type == NodeType::Terminal);

    int pot = n.pot;
    int half_pot = pot / 2;  // each player's contribution

    if (n.fold_player >= 0) {
        // Someone folded. The non-folding player wins the pot.
        int folder = n.fold_player;
        int winner = 1 - folder;
        // Payoff = (pot won - amount invested) for the traverser.
        if (traverser == winner) {
            return static_cast<float>(half_pot);   // win opponent's contribution
        } else {
            return static_cast<float>(-half_pot);  // lose our contribution
        }
    }

    // Showdown: evaluate hands.
    uint16_t rank0 = eval_.evaluate(hole_[0], board_);
    uint16_t rank1 = eval_.evaluate(hole_[1], board_);

    if (rank0 > rank1) {
        // Player 0 wins.
        return traverser == 0 ? static_cast<float>(half_pot)
                              : static_cast<float>(-half_pot);
    } else if (rank1 > rank0) {
        // Player 1 wins.
        return traverser == 1 ? static_cast<float>(half_pot)
                              : static_cast<float>(-half_pot);
    } else {
        // Tie: split pot, net zero.
        return 0.0f;
    }
}

// ── DCFR discounting ─────────────────────────────────────────────────────

void CFRSolver::applyDiscounting() {
    // DCFR discounts are applied each iteration.
    // For iteration t (1-indexed):
    //   positive_regret_weight = t^alpha / (t^alpha + 1)
    //   negative_regret_weight = t^beta  / (t^beta + 1)
    //   strategy_weight        = (t / (t + 1))^gamma
    //
    // We apply these as multiplicative factors to all entries.

    uint64_t t = iteration_ + 1;  // 1-indexed

    float t_f = static_cast<float>(t);
    float t_alpha = std::pow(t_f, params_.alpha);
    float t_beta  = std::pow(t_f, params_.beta);

    float pos_weight = t_alpha / (t_alpha + 1.0f);
    float neg_weight = t_beta  / (t_beta  + 1.0f);
    float strat_weight = std::pow(t_f / (t_f + 1.0f), params_.gamma);

    uint64_t total = store_->totalEntries();

    for (int p = 0; p < 2; ++p) {
        float* reg = store_->regrets(p);
        float* strat = store_->strategy(p);

        for (uint64_t i = 0; i < total; ++i) {
            // Discount regrets: different weight for positive vs negative.
            if (reg[i] > 0.0f) {
                reg[i] *= pos_weight;
            } else {
                reg[i] *= neg_weight;
            }

            // Discount strategy sums.
            strat[i] *= strat_weight;
        }
    }
}
