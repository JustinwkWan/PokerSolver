#include "abstraction/action_abstraction.h"

#include <algorithm>
#include <cmath>

// ── Default config ──────────────────────────────────────────────────────
BetSizeConfig BetSizeConfig::defaultConfig() {
    return {
        /* preflop */ {0.5, 1.0},
        /* flop    */ {0.5, 1.0},
        /* turn    */ {0.5, 1.0},
        /* river   */ {0.5, 1.0},
    };
}

// ── ActionAbstraction ───────────────────────────────────────────────────
ActionAbstraction::ActionAbstraction(const BetSizeConfig& config)
    : config_(config) {}

const std::vector<double>& ActionAbstraction::sizesForStreet(Street s) const {
    switch (s) {
        case Street::Preflop: return config_.preflop;
        case Street::Flop:    return config_.flop;
        case Street::Turn:    return config_.turn;
        case Street::River:   return config_.river;
    }
    return config_.flop;  // unreachable
}

std::vector<Action> ActionAbstraction::getActions(const GameState& state) const {
    auto legal = state.legalActions();
    std::vector<Action> actions;
    actions.reserve(8);

    // ── Always-available actions ────────────────────────────────────────
    if (legal.can_fold)  actions.push_back(Action::fold());
    if (legal.can_check) actions.push_back(Action::check());
    if (legal.can_call)  actions.push_back(Action::call());

    // ── Bet/raise sizes ────────────────────────────────────────────────
    if (!legal.can_bet_raise) return actions;

    int min_raise = legal.min_bet_raise;
    int max_raise = legal.max_bet_raise;

    // If min == max, there's only one possible raise (all-in).
    if (min_raise == max_raise) {
        actions.push_back(Action::betRaise(max_raise));
        return actions;
    }

    const auto& fractions = sizesForStreet(state.street);

    int pot = state.potSize();
    int call_amount = legal.call_amount;
    int pot_after_call = pot + call_amount;
    int my_bet = state.street_bet[state.actor];

    // Collect candidate raise-to amounts (excluding all-in, added separately).
    std::vector<int> candidates;
    candidates.reserve(fractions.size());

    for (double frac : fractions) {
        double bet_chips = frac * pot_after_call;
        int total = my_bet + call_amount +
                    static_cast<int>(std::round(bet_chips));

        // Clamp to legal range.
        if (total < min_raise) total = min_raise;
        if (total > max_raise) total = max_raise;

        // Skip if it rounds to all-in (we add all-in separately).
        if (total == max_raise) continue;

        candidates.push_back(total);
    }

    // Deduplicate.
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());

    for (int amt : candidates)
        actions.push_back(Action::betRaise(amt));

    // Always include all-in.
    actions.push_back(Action::betRaise(max_raise));

    return actions;
}
