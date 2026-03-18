#pragma once

#include "core/game_state.h"

#include <vector>

// ── Bet size configuration ──────────────────────────────────────────────
// Bet sizes as fractions of pot, per street.
// All-in is always included implicitly — do not add it here.
// Example: {0.5, 1.0} means half-pot and pot-sized bets, plus all-in.

struct BetSizeConfig {
    std::vector<double> preflop;
    std::vector<double> flop;
    std::vector<double> turn;
    std::vector<double> river;

    // Default: 0.5x pot, 1.0x pot (+ all-in) at every street.
    static BetSizeConfig defaultConfig();
};

// ── Action abstraction ──────────────────────────────────────────────────
// Given a game state, produces the set of abstract actions a player may take.
//
// For each bet/raise, the pot-fraction is converted to a chip amount:
//   call_amount  = chips needed to call (0 if opening)
//   pot_after_call = potSize() + call_amount
//   bet_chips    = fraction * pot_after_call
//   total_committed = street_bet[actor] + call_amount + bet_chips
//
// The result is clamped to [min_raise, max_raise]. Duplicates and sizes
// below min_raise are removed. All-in is always included as a distinct action.
 
class ActionAbstraction {
public:
    explicit ActionAbstraction(
        const BetSizeConfig& config = BetSizeConfig::defaultConfig());

    // Return the abstract action set for the current state.
    // Includes fold/check/call as appropriate, plus bet/raise sizes.
    std::vector<Action> getActions(const GameState& state) const;

    const BetSizeConfig& config() const { return config_; }

private:
    BetSizeConfig config_;

    const std::vector<double>& sizesForStreet(Street s) const;
};