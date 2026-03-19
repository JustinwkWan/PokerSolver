#pragma once

#include "core/game_state.h"  // for kMaxPlayers
#include "tree/info_set.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// ── RegretStore ──────────────────────────────────────────────────────────
// Memory-mapped storage for cumulative regrets and strategy sums.
//
// Layout: 4 flat arrays of float32, one per (player, type):
//   regrets[player][info_set_id * max_actions + action]
//   strategy[player][info_set_id * max_actions + action]
//
// Backed by mmap'd files for:
//   - OS-managed paging (don't need full blueprint in RAM)
//   - Crash-safe checkpointing (msync + rename)
//   - Resume from checkpoint
//
// The store does NOT know about the game tree — it's a flat array indexed
// by (info_set_id, action). The InfoSetManager provides the mapping from
// (node, bucket) → info_set_id.

class RegretStore {
public:
    // Create a new store with the given sizing. If dir is non-empty, files are
    // created in that directory. If dir is empty, uses anonymous mmap (in-memory).
    RegretStore(uint64_t num_info_sets, int max_actions,
                const std::string& dir = "", int num_players = 2);

    // Load an existing store from a directory (checkpoint resume).
    static RegretStore load(const std::string& dir);

    ~RegretStore();

    // Non-copyable, movable.
    RegretStore(const RegretStore&) = delete;
    RegretStore& operator=(const RegretStore&) = delete;
    RegretStore(RegretStore&& other) noexcept;
    RegretStore& operator=(RegretStore&& other) noexcept;

    // ── Access ────────────────────────────────────────────────────────────

    // Raw pointer to regrets for a player. Indexed as [info_set_id * max_actions + action].
    float* regrets(int player)             { return regrets_[player]; }
    const float* regrets(int player) const { return regrets_[player]; }

    // Raw pointer to cumulative strategy sums for a player.
    float* strategy(int player)             { return strategy_[player]; }
    const float* strategy(int player) const { return strategy_[player]; }

    // Convenience: pointer to the start of a specific info set's regret/strategy slice.
    float* regretSlice(int player, uint64_t info_set_id) {
        return regrets_[player] + info_set_id * max_actions_;
    }
    float* strategySlice(int player, uint64_t info_set_id) {
        return strategy_[player] + info_set_id * max_actions_;
    }
    const float* regretSlice(int player, uint64_t info_set_id) const {
        return regrets_[player] + info_set_id * max_actions_;
    }
    const float* strategySlice(int player, uint64_t info_set_id) const {
        return strategy_[player] + info_set_id * max_actions_;
    }

    // ── Regret matching ──────────────────────────────────────────────────

    // Compute the current strategy from positive regrets via regret matching.
    // Writes result into `out` (must have at least num_actions elements).
    // If all regrets are non-positive, returns uniform distribution.
    void getStrategy(int player, uint64_t info_set_id, int num_actions,
                     float* out) const;

    // Compute the average strategy from cumulative strategy sums.
    // If all sums are zero, returns uniform distribution.
    void getAverageStrategy(int player, uint64_t info_set_id, int num_actions,
                            float* out) const;

    // ── Persistence ──────────────────────────────────────────────────────

    // Flush mapped pages to disk (non-blocking by default).
    void sync(bool blocking = false);

    // Save a checkpoint: sync + write metadata file.
    void checkpoint(const std::string& dir, uint64_t iteration) const;

    // ── Sizing ────────────────────────────────────────────────────────────

    uint64_t numInfoSets() const { return num_info_sets_; }
    int maxActions() const { return max_actions_; }
    uint64_t totalEntries() const { return num_info_sets_ * max_actions_; }
    uint64_t bytesPerArray() const { return totalEntries() * sizeof(float); }
    int numPlayers() const { return num_players_; }
    uint64_t totalBytes() const { return bytesPerArray() * num_players_ * 2; }

private:
    uint64_t num_info_sets_ = 0;
    int max_actions_ = 0;
    int num_players_ = 2;

    // Pointers into mmap'd regions (or heap-allocated for anonymous mode).
    // regrets_[p] and strategy_[p] for each player p.
    std::array<float*, kMaxPlayers> regrets_  = {};
    std::array<float*, kMaxPlayers> strategy_ = {};

    // mmap bookkeeping: regions_[p] = regrets for player p,
    //                   regions_[num_players_ + p] = strategy for player p.
    struct MmapRegion {
        void* addr = nullptr;
        size_t size = 0;
        int fd = -1;
    };
    std::array<MmapRegion, kMaxPlayers * 2> regions_;
    bool anonymous_ = true;
    std::string dir_;

    RegretStore() = default;  // for load()
    void mapFile(int idx, const std::string& path, bool create);
    void unmapAll();
};
