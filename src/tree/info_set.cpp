#include "tree/info_set.h"

#include <algorithm>
#include <cassert>

InfoSetManager::InfoSetManager(const GameTree& tree,
                               const std::array<int, 4>& buckets_per_street)
    : tree_(&tree), buckets_per_street_(buckets_per_street) {

    uint32_t num_base = tree.numInfoSets();
    node_offset_.resize(num_base);
    base_num_actions_.resize(num_base);
    base_player_.resize(num_base);
    base_street_.resize(num_base);

    // Scan all action nodes to populate per-base-info-set metadata.
    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type != NodeType::Action) continue;

        uint32_t base = n.info_set_idx;
        assert(base < num_base);

        base_num_actions_[base] = n.num_actions;
        base_player_[base]      = n.player;
        base_street_[base]      = n.street;

        if (n.num_actions > max_actions_)
            max_actions_ = n.num_actions;
    }

    // Compute cumulative offsets.
    uint64_t offset = 0;
    for (uint32_t i = 0; i < num_base; ++i) {
        node_offset_[i] = offset;
        offset += buckets_per_street_[base_street_[i]];
    }
    total_info_sets_ = offset;
}

uint64_t InfoSetManager::infoSetId(uint32_t node_idx, int bucket_id) const {
    const auto& n = tree_->node(node_idx);
    assert(n.type == NodeType::Action);
    assert(bucket_id >= 0 && bucket_id < buckets_per_street_[n.street]);
    return node_offset_[n.info_set_idx] + bucket_id;
}

int InfoSetManager::numActions(uint32_t node_idx) const {
    return tree_->node(node_idx).num_actions;
}

int InfoSetManager::numActionsForBase(uint32_t base_idx) const {
    assert(base_idx < base_num_actions_.size());
    return base_num_actions_[base_idx];
}

int InfoSetManager::playerForBase(uint32_t base_idx) const {
    assert(base_idx < base_player_.size());
    return base_player_[base_idx];
}

int InfoSetManager::streetForBase(uint32_t base_idx) const {
    assert(base_idx < base_street_.size());
    return base_street_[base_idx];
}
