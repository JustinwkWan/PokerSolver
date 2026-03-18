#include "solver/cfr.h"

#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <string>

// ── Kuhn poker ─────────────────────────────────────────────────────────────
// 3 cards: J(0), Q(1), K(2). 2 players, 1 card each, ante 1 chip.
// Two actions at every node: p(pass) and b(bet).
//   pass = check if no bet, fold if facing a bet
//   bet  = bet 1 if no bet, call if facing a bet
//
// 12 info sets. Nash game value = -1/18 for P1.

static const char* CARD_NAME[] = {"J", "Q", "K"};

static bool is_terminal(const std::string& h) {
    return h == "pp" || h == "pbb" || h == "pbp" || h == "bb" || h == "bp";
}

// Returns utility for the CURRENT player (player = len(h) % 2).
static double terminal_payoff(int cards[2], const std::string& h) {
    int player = static_cast<int>(h.size()) % 2;
    if (h == "pp") {
        int w = (cards[0] > cards[1]) ? 0 : 1;
        return (w == player) ? 1.0 : -1.0;
    }
    if (h == "pbb" || h == "bb") {
        int w = (cards[0] > cards[1]) ? 0 : 1;
        return (w == player) ? 2.0 : -2.0;
    }
    if (h == "pbp") return (player == 0) ? -1.0 : 1.0;  // P0 folded
    if (h == "bp")  return (player == 0) ? 1.0 : -1.0;   // P1 folded
    return 0;
}

// ── CFR traversal ──────────────────────────────────────────────────────────
// Returns utility for the current player. Negate on recursion.

static double cfr(CFRTrainer& trainer, int cards[2],
                  const std::string& history, double pi0, double pi1) {
    if (is_terminal(history))
        return terminal_payoff(cards, history);

    int player = static_cast<int>(history.size()) % 2;
    std::string key = std::string(CARD_NAME[cards[player]]) + ":" + history;

    InfoSet& info = trainer.get(key, 2);
    auto strategy = info.getStrategy();

    double action_util[2] = {};
    double node_util = 0;

    for (int a = 0; a < 2; ++a) {
        std::string next = history + (a == 0 ? "p" : "b");
        if (player == 0)
            action_util[a] = -cfr(trainer, cards, next, pi0 * strategy[a], pi1);
        else
            action_util[a] = -cfr(trainer, cards, next, pi0, pi1 * strategy[a]);
        node_util += strategy[a] * action_util[a];
    }

    double cf_reach = (player == 0) ? pi1 : pi0;
    for (int a = 0; a < 2; ++a)
        info.cumulative_regret[a] += cf_reach * (action_util[a] - node_util);

    double reach = (player == 0) ? pi0 : pi1;
    for (int a = 0; a < 2; ++a)
        info.cumulative_strategy[a] += reach * strategy[a];

    return node_util;
}

// ── Best response (info-set-constrained) ───────────────────────────────────
// Traverses with a probability distribution over opponent cards.
// At br_player's nodes: picks the best action (same across all opp cards).
// At opponent's nodes: marginalizes over opp cards using their avg strategy.

static double br_traverse(CFRTrainer& trainer, int br_card, double opp_prob[3],
                          const std::string& history, int br_player) {
    if (is_terminal(history)) {
        double val = 0;
        int player = static_cast<int>(history.size()) % 2;
        for (int opp = 0; opp < 3; ++opp) {
            if (opp_prob[opp] <= 0) continue;
            int cards[2];
            cards[br_player] = br_card;
            cards[1 - br_player] = opp;
            double pval = terminal_payoff(cards, history);
            double br_val = (player == br_player) ? pval : -pval;
            val += opp_prob[opp] * br_val;
        }
        return val;
    }

    int player = static_cast<int>(history.size()) % 2;

    if (player == br_player) {
        // Best response: pick action maximizing expected utility
        double best = -1e18;
        for (int a = 0; a < 2; ++a) {
            std::string next = history + (a == 0 ? "p" : "b");
            best = std::max(best, br_traverse(trainer, br_card, opp_prob, next, br_player));
        }
        return best;
    } else {
        // Opponent's turn: branch by action, update opp_prob via Bayes
        double total = 0;
        for (int a = 0; a < 2; ++a) {
            double new_opp_prob[3] = {};
            for (int opp = 0; opp < 3; ++opp) {
                if (opp_prob[opp] <= 0) continue;
                std::string key = std::string(CARD_NAME[opp]) + ":" + history;
                auto avg = trainer.get(key, 2).getAverageStrategy();
                new_opp_prob[opp] = opp_prob[opp] * avg[a];
            }
            std::string next = history + (a == 0 ? "p" : "b");
            total += br_traverse(trainer, br_card, new_opp_prob, next, br_player);
        }
        return total;
    }
}

static double compute_exploitability(CFRTrainer& trainer) {
    double total[2] = {};
    for (int br_player = 0; br_player < 2; ++br_player) {
        for (int br_card = 0; br_card < 3; ++br_card) {
            double opp_prob[3];
            for (int opp = 0; opp < 3; ++opp)
                opp_prob[opp] = (opp == br_card) ? 0.0 : 0.5;
            total[br_player] += br_traverse(trainer, br_card, opp_prob, "", br_player);
        }
    }
    // total[p] = sum over 3 cards of expected utility when p best-responds
    // Average over the 3 cards (each equally likely)
    return (total[0] / 3.0 + total[1] / 3.0) / 2.0;
}

// ── Tests ──────────────────────────────────────────────────────────────────

TEST(Kuhn, ConvergesToNashValue) {
    CFRTrainer trainer;
    constexpr int ITERS = 100'000;

    double total_util = 0;
    for (int t = 0; t < ITERS; ++t) {
        for (int c0 = 0; c0 < 3; ++c0)
            for (int c1 = 0; c1 < 3; ++c1) {
                if (c0 == c1) continue;
                int cards[2] = {c0, c1};
                total_util += cfr(trainer, cards, "", 1.0, 1.0);
            }
    }

    double game_value = total_util / (ITERS * 6);
    EXPECT_NEAR(game_value, -1.0 / 18.0, 0.01);

    double exploit = compute_exploitability(trainer);
    EXPECT_LT(exploit, 0.001)
        << "Exploitability after " << ITERS << " iterations: " << exploit;
}

TEST(Kuhn, StrategyProperties) {
    CFRTrainer trainer;
    for (int t = 0; t < 100'000; ++t)
        for (int c0 = 0; c0 < 3; ++c0)
            for (int c1 = 0; c1 < 3; ++c1) {
                if (c0 == c1) continue;
                int cards[2] = {c0, c1};
                cfr(trainer, cards, "", 1.0, 1.0);
            }

    // P1 with J at root: bets at most 1/3 (any alpha in [0,1/3] is Nash)
    auto j_root = trainer.get("J:", 2).getAverageStrategy();
    EXPECT_LT(j_root[1], 0.4) << "P1 with J should bet at most ~1/3";

    // P1 with K at root: bets frequently
    auto k_root = trainer.get("K:", 2).getAverageStrategy();
    EXPECT_GT(k_root[1], 0.5) << "P1 with K should bet frequently at root";

    // P1 with J facing a bet ("J:pb"): should fold most of the time
    auto j_facing = trainer.get("J:pb", 2).getAverageStrategy();
    EXPECT_GT(j_facing[0], 0.5) << "P1 with J should mostly fold to a bet";
}
