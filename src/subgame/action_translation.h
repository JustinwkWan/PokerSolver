#pragma once

#include "core/game_state.h"

#include <vector>

// ── ActionTranslation ────────────────────────────────────────────────────
// Maps off-abstraction bet sizes to nearby abstract sizes probabilistically.
//
// When an opponent bets a size not in our abstraction, we need to translate
// it to actions we have a strategy for. Pseudo-harmonic mapping distributes
// probability between the two nearest abstract sizes:
//
//   Given actual bet x between abstract sizes A and B:
//     P(map to A) = (x - A)^{-1} / ((x - A)^{-1} + (B - x)^{-1})
//                 = (B - x) / (B - A)... wait, that's linear.
//
//   Pseudo-harmonic:
//     P(A) = (B - x) / (x - A + B - x) ... no.
//     Correct formula uses inverse distances:
//     P(A) = 1/(x - A) / (1/(x - A) + 1/(B - x))
//          = (B - x) / ((B - x) + (x - A))  ... simplifies to linear!
//
//   Actually the pseudo-harmonic formula IS:
//     P(A) = (x - A)^{-1} / ((x - A)^{-1} + (B - x)^{-1})
//          = (B - x) / (B - A)
//
//   This is equivalent to linear interpolation but expressed via inverse
//   distances. The key properties: boundary (P(A)=1 when x=A), monotone,
//   and scale-invariant.
//
// If the bet is below the smallest abstract size or above the largest,
// it maps entirely to the nearest boundary action.

struct TranslationResult {
    // The two nearest abstract action indices and their probabilities.
    // If the bet matches exactly or falls outside, only one entry is used
    // (prob_lo = 1.0, prob_hi = 0.0).
    int action_lo;    // index of smaller abstract action
    int action_hi;    // index of larger abstract action
    float prob_lo;    // probability of using action_lo
    float prob_hi;    // probability of using action_hi
};

class ActionTranslation {
public:
    // Translate an actual bet amount to nearby abstract actions.
    //
    // abstract_amounts: sorted list of abstract bet/raise-to amounts.
    //                   Must include all-in as the largest entry.
    // actual_amount:    the opponent's actual bet/raise-to amount.
    //
    // Returns which abstract action(s) to map to and with what probability.
    static TranslationResult translate(const std::vector<int>& abstract_amounts,
                                        int actual_amount);

    // Translate an action in the context of a game state.
    // Given an actual action and the abstract actions available at this node,
    // returns the translation mapping.
    //
    // If the action type is fold/check/call, it maps directly (no translation).
    // Only bet/raise amounts need translation.
    static TranslationResult translateAction(const Action& actual_action,
                                              const std::vector<Action>& abstract_actions);
};
