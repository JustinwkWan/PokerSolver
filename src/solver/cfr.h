#pragma once

#include <algorithm>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

// ── Info set data ──────────────────────────────────────────────────────────
// Stores cumulative regrets and strategy sums for one information set.
// Games call getStrategy() each iteration (regret matching) and accumulate
// regrets/strategy externally.

struct InfoSet {
    std::vector<double> cumulative_regret;
    std::vector<double> cumulative_strategy;
    int num_actions;

    explicit InfoSet(int n)
        : cumulative_regret(n, 0.0), cumulative_strategy(n, 0.0), num_actions(n) {}

    // Regret matching: strategy proportional to positive regrets.
    std::vector<double> getStrategy() const {
        std::vector<double> strat(num_actions);
        double sum = 0;
        for (int a = 0; a < num_actions; ++a) {
            strat[a] = std::max(0.0, cumulative_regret[a]);
            sum += strat[a];
        }
        if (sum > 0)
            for (int a = 0; a < num_actions; ++a) strat[a] /= sum;
        else
            for (int a = 0; a < num_actions; ++a) strat[a] = 1.0 / num_actions;
        return strat;
    }

    // Average strategy over all iterations (the Nash approximation).
    std::vector<double> getAverageStrategy() const {
        std::vector<double> avg(num_actions);
        double sum = std::accumulate(cumulative_strategy.begin(),
                                     cumulative_strategy.end(), 0.0);
        if (sum > 0)
            for (int a = 0; a < num_actions; ++a) avg[a] = cumulative_strategy[a] / sum;
        else
            for (int a = 0; a < num_actions; ++a) avg[a] = 1.0 / num_actions;
        return avg;
    }
};

// ── CFR trainer ────────────────────────────────────────────────────────────
// Just a hash map of info sets. Games implement their own tree traversal
// and call get() to access/create info sets on the fly.

class CFRTrainer {
public:
    std::unordered_map<std::string, InfoSet> info_sets;

    InfoSet& get(const std::string& key, int num_actions) {
        auto it = info_sets.find(key);
        if (it == info_sets.end())
            it = info_sets.emplace(key, InfoSet(num_actions)).first;
        return it->second;
    }
};
