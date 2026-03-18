#pragma once

#include "tree/game_tree.h"

#include <array>
#include <cstdint>
#include <vector>

// ── InfoSetManager ──────────────────────────────────────────────────────
// Maps (tree_node_index, bucket_id) → flat info set index for regret/strategy
// arrays. Each action node in the tree has a base info_set_idx; the full ID
// incorporates the card bucket:
//
//   flat_id = node_offset[info_set_idx] + bucket_id
//
// Different streets have different bucket counts, so node_offset accumulates
// correctly per-street.

class InfoSetManager {
public:
    // buckets_per_street: [preflop, flop, turn, river] bucket counts.
    InfoSetManager(const GameTree& tree,
                   const std::array<int, 4>& buckets_per_street);

    // ── Lookup ──────────────────────────────────────────────────────────

    // Flat info set index for a given action node and bucket.
    uint64_t infoSetId(uint32_t node_idx, int bucket_id) const;

    // Number of actions at a given action node.
    int numActions(uint32_t node_idx) const;

    // Number of actions for a given base info set index (0..numInfoSets()-1).
    int numActionsForBase(uint32_t base_idx) const;

    // Player who acts at a given base info set index.
    int playerForBase(uint32_t base_idx) const;

    // Street for a given base info set index.
    int streetForBase(uint32_t base_idx) const;

    // ── Sizing ──────────────────────────────────────────────────────────

    // Total number of unique (node, bucket) info sets across the tree.
    uint64_t totalInfoSets() const { return total_info_sets_; }

    // Maximum actions across all action nodes.
    int maxActions() const { return max_actions_; }

    // Total regret/strategy entries: totalInfoSets() * maxActions().
    uint64_t totalEntries() const { return total_info_sets_ * max_actions_; }

    // Bucket count for a given street.
    int bucketsForStreet(int street) const { return buckets_per_street_[street]; }

    // Base info set count (= number of action nodes in tree).
    uint32_t numBaseInfoSets() const {
        return static_cast<uint32_t>(node_offset_.size());
    }

private:
    const GameTree* tree_;
    std::array<int, 4> buckets_per_street_;

    // node_offset_[base_info_set_idx] = starting flat index for that node.
    std::vector<uint64_t> node_offset_;

    // Per base info set: number of actions, player, street.
    std::vector<uint8_t> base_num_actions_;
    std::vector<uint8_t> base_player_;
    std::vector<uint8_t> base_street_;

    uint64_t total_info_sets_ = 0;
    int max_actions_ = 0;
};
