#pragma once

#include "abstraction/action_abstraction.h"
#include "core/game_state.h"

#include <cstdint>
#include <vector>

// ── Node types ──────────────────────────────────────────────────────────
enum class NodeType : uint8_t { Action, Terminal };

// A node in the abstract game tree.
// Action nodes represent player decision points.
// Terminal nodes represent fold or showdown outcomes.
// Chance nodes (card deals) are NOT explicit — cards are sampled externally
// during CFR traversal.
struct TreeNode {
    NodeType type;
    uint8_t  player;        // 0 or 1 (for action nodes)
    uint8_t  street;        // 0=preflop, 1=flop, 2=turn, 3=river
    uint8_t  num_actions;   // number of children (0 for terminal)

    uint32_t children_start; // index into children/actions arrays
    uint32_t info_set_idx;   // unique per action node; base index (add bucket for full ID)

    // Terminal node fields
    int      pot;            // total pot at terminal
    int8_t   fold_player;    // 0 or 1 if someone folded, -1 for showdown
};

// ── GameTree ────────────────────────────────────────────────────────────
// Builds and stores an explicit abstract game tree as flat arrays.
//
// The tree captures all possible action sequences (fold/check/call/raise)
// using the given ActionAbstraction. Card deals are implicit — during CFR,
// cards are sampled externally and the tree is traversed with bucket IDs.
//
// Each action node gets a unique info_set_idx. The full info set ID during
// CFR is: info_set_idx * num_buckets_for_street + bucket_id.

class GameTree {
public:
    GameTree(int stack_size, const ActionAbstraction& aa);

    void build();

    // ── Access ──────────────────────────────────────────────────────────
    const TreeNode& root() const       { return nodes_[0]; }
    const TreeNode& node(uint32_t i) const { return nodes_[i]; }

    uint32_t child(uint32_t node_idx, int action_idx) const {
        return children_[nodes_[node_idx].children_start + action_idx];
    }

    const Action& action(uint32_t node_idx, int action_idx) const {
        return actions_[nodes_[node_idx].children_start + action_idx];
    }

    // ── Stats ───────────────────────────────────────────────────────────
    uint32_t numNodes() const          { return static_cast<uint32_t>(nodes_.size()); }
    uint32_t numActionNodes() const    { return num_action_nodes_; }
    uint32_t numTerminalNodes() const  { return num_terminal_nodes_; }
    uint32_t numInfoSets() const       { return info_set_counter_; }

private:
    std::vector<TreeNode>   nodes_;
    std::vector<uint32_t>   children_;  // children_[children_start + i] = child node index
    std::vector<Action>     actions_;   // actions_[children_start + i]  = action for child i

    int stack_size_;
    ActionAbstraction aa_;

    uint32_t info_set_counter_  = 0;
    uint32_t num_action_nodes_  = 0;
    uint32_t num_terminal_nodes_ = 0;

    // Recursively build the tree from a game state.
    // Uses dummy cards (since legal actions don't depend on card values).
    uint32_t buildNode(GameState state);

    // Deal dummy cards to advance past chance nodes.
    static GameState dealDummyCards(GameState state);
};
