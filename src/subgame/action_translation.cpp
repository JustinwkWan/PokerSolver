#include "subgame/action_translation.h"

#include <algorithm>
#include <cassert>

// ── translate (amount-based) ─────────────────────────────────────────────

TranslationResult ActionTranslation::translate(
        const std::vector<int>& abstract_amounts, int actual_amount) {
    assert(!abstract_amounts.empty());

    int n = static_cast<int>(abstract_amounts.size());

    // Exact match: find it and return 100%.
    for (int i = 0; i < n; ++i) {
        if (abstract_amounts[i] == actual_amount) {
            return {i, i, 1.0f, 0.0f};
        }
    }

    // Below smallest abstract amount: map entirely to smallest.
    if (actual_amount < abstract_amounts[0]) {
        return {0, 0, 1.0f, 0.0f};
    }

    // Above largest abstract amount: map entirely to largest.
    if (actual_amount > abstract_amounts[n - 1]) {
        return {n - 1, n - 1, 1.0f, 0.0f};
    }

    // Find the bracket: abstract_amounts[lo] < actual_amount < abstract_amounts[hi].
    int lo = -1;
    for (int i = 0; i < n - 1; ++i) {
        if (abstract_amounts[i] < actual_amount &&
            actual_amount < abstract_amounts[i + 1]) {
            lo = i;
            break;
        }
    }
    assert(lo >= 0);
    int hi = lo + 1;

    // Pseudo-harmonic interpolation:
    //   P(lo) = 1/(x - A) / (1/(x - A) + 1/(B - x))
    //         = (B - x) / ((B - x) + (x - A))
    //         = (B - x) / (B - A)
    float A = static_cast<float>(abstract_amounts[lo]);
    float B = static_cast<float>(abstract_amounts[hi]);
    float x = static_cast<float>(actual_amount);

    float p_lo = (B - x) / (B - A);
    float p_hi = 1.0f - p_lo;

    return {lo, hi, p_lo, p_hi};
}

// ── translateAction (action-based) ───────────────────────────────────────

TranslationResult ActionTranslation::translateAction(
        const Action& actual_action,
        const std::vector<Action>& abstract_actions) {
    assert(!abstract_actions.empty());

    // For non-bet/raise actions, find exact match.
    if (actual_action.type != ActionType::BetRaise) {
        for (int i = 0; i < static_cast<int>(abstract_actions.size()); ++i) {
            if (abstract_actions[i].type == actual_action.type) {
                return {i, i, 1.0f, 0.0f};
            }
        }
        // Action type not found in abstract actions (shouldn't happen in
        // well-formed trees). Map to first action as fallback.
        return {0, 0, 1.0f, 0.0f};
    }

    // Bet/raise: collect all abstract bet/raise amounts.
    std::vector<int> amounts;
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(abstract_actions.size()); ++i) {
        if (abstract_actions[i].type == ActionType::BetRaise) {
            amounts.push_back(abstract_actions[i].amount);
            indices.push_back(i);
        }
    }

    if (amounts.empty()) {
        // No bet/raise in abstract actions. Map to last action (fallback).
        int last = static_cast<int>(abstract_actions.size()) - 1;
        return {last, last, 1.0f, 0.0f};
    }

    // Translate the amount.
    auto result = translate(amounts, actual_action.amount);

    // Map internal indices back to action indices.
    result.action_lo = indices[result.action_lo];
    result.action_hi = indices[result.action_hi];

    return result;
}
