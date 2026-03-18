#include "solver/cfr.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ── Leduc poker ────────────────────────────────────────────────────────────
// Deck: J(0), Q(1), K(2) in two suits → 6 cards.
// 2 players, 1 hole card each, ante 1 chip.
// Round 1: bet = 2, max 1 raise (so max 2 bets).
// Deal 1 community card.
// Round 2: bet = 4, max 1 raise.
// Showdown: pair (hole == community) > non-pair. Higher card wins ties.
//
// Actions within a round:
//   k = check, b = bet, c = call, r = raise, f = fold
//
// Round ends after: kk, kbc, kbf, kbrc, kbrf, bc, bf, brc, brf
//
// Info set key:
//   Round 1: "<hole_rank>:<round1_actions>"
//   Round 2: "<hole_rank><community_rank>:<round1_actions>/<round2_actions>"

static const int BET_SIZE[] = {2, 4};  // round 1, round 2

// ── Round-level helpers ────────────────────────────────────────────────────

struct RoundResult {
    bool done;       // round/hand ended
    bool folded;     // someone folded
    int  folder;     // who folded (0 or 1)
    int  player;     // whose turn if not done
    std::vector<char> actions;  // legal actions
};

static RoundResult analyse_round(const std::string& round_hist) {
    RoundResult r{};
    int len = static_cast<int>(round_hist.size());

    // Terminal round sequences
    if (round_hist == "kk" || round_hist == "kbc" || round_hist == "kbrc" ||
        round_hist == "bc" || round_hist == "brc") {
        r.done = true; r.folded = false; return r;
    }
    if (round_hist == "kbf") {
        r.done = true; r.folded = true;
        // Who folded? 'kbf': k=P1, b=P2, f=P1 → P1 folds
        r.folder = 0; return r;
    }
    if (round_hist == "kbrf") {
        r.done = true; r.folded = true;
        // k=P1, b=P2, r=P1, f=P2 → P2 folds
        r.folder = 1; return r;
    }
    if (round_hist == "bf") {
        r.done = true; r.folded = true;
        // b=P1, f=P2 → P2 folds
        r.folder = 1; return r;
    }
    if (round_hist == "brf") {
        r.done = true; r.folded = true;
        // b=P1, r=P2, f=P1 → P1 folds
        r.folder = 0; return r;
    }

    r.done = false;
    r.player = len % 2;  // P1(0) acts at even positions, P2(1) at odd

    // Determine legal actions based on state
    if (len == 0) {
        r.actions = {'k', 'b'};                   // check or bet
    } else if (round_hist == "k") {
        r.actions = {'k', 'b'};                   // check or bet
    } else if (round_hist == "kb" || round_hist == "b") {
        r.actions = {'f', 'c', 'r'};              // fold, call, or raise
    } else if (round_hist == "kbr" || round_hist == "br") {
        r.actions = {'f', 'c'};                   // fold or call (max raises hit)
    } else {
        // Shouldn't reach here
        r.done = true;
    }

    return r;
}

// Compute each player's total investment from the full action history.
static void compute_investments(const std::string& r1, const std::string& r2,
                                int inv[2]) {
    inv[0] = inv[1] = 1;  // antes

    auto add_round = [&](const std::string& hist, int bet_size) {
        int p = 0;  // player index within round
        for (char ch : hist) {
            int who = p % 2;
            if (ch == 'b') inv[who] += bet_size;
            else if (ch == 'c') {
                // Call: match the opponent
                int diff = inv[1 - who] - inv[who];
                inv[who] += diff;
            }
            else if (ch == 'r') {
                // Raise: call + raise
                int diff = inv[1 - who] - inv[who];
                inv[who] += diff + bet_size;
            }
            ++p;
        }
    };

    add_round(r1, BET_SIZE[0]);
    add_round(r2, BET_SIZE[1]);
}

// Showdown winner: pair > non-pair, then higher card.
// Returns +1 if P0 wins, -1 if P1 wins, 0 if tie (impossible in Leduc).
static int showdown_winner(int hole0, int hole1, int community) {
    bool pair0 = (hole0 == community);
    bool pair1 = (hole1 == community);
    if (pair0 && !pair1) return 1;
    if (!pair0 && pair1) return -1;
    // Both pair or both non-pair: higher hole card wins
    if (hole0 > hole1) return 1;
    if (hole0 < hole1) return -1;
    return 0;  // tie (same rank, different suits)
}

// ── CFR traversal ──────────────────────────────────────────────────────────
// Returns utility for the ACTING player at this node.
// Uses negate-on-recursion (same convention as Kuhn test).

static double cfr(CFRTrainer& trainer,
                  int hole[2], int community,
                  const std::string& r1, const std::string& r2,
                  int round,
                  double pi0, double pi1) {
    const std::string& cur = (round == 1) ? r1 : r2;
    RoundResult rr = analyse_round(cur);
    int cur_player = static_cast<int>(cur.size()) % 2;

    // ── Fold terminal ─────────────────────────────────────────────────────
    if (rr.done && rr.folded) {
        int inv[2];
        compute_investments(r1, r2, inv);
        // Utility for the "current player" at this terminal
        // cur_player = len % 2 = the player who would act next
        // After a fold, the last actor folded. Next player is the non-folder.
        // cur_player IS the non-folder (winner).
        if (rr.folder == 0)
            return (cur_player == 0) ? -inv[0] : inv[0];   // P0 folded
        else
            return (cur_player == 0) ? inv[1] : -inv[1];   // P1 folded
    }

    // ── Round over, not folded ────────────────────────────────────────────
    if (rr.done && !rr.folded) {
        if (round == 1) {
            // Chance node: deal community card, recurse into round 2
            // Iterate over individual card copies so that regret updates
            // inside each recursive call are naturally weighted by the
            // community card's probability (cards with 2 remaining copies
            // contribute 2x the regret vs cards with 1 copy).
            double val = 0;
            int remaining = 0;
            for (int cc = 0; cc < 3; ++cc) {
                int copies = 2;
                if (cc == hole[0]) copies--;
                if (cc == hole[1]) copies--;
                if (copies <= 0) continue;
                remaining += copies;
            }
            for (int cc = 0; cc < 3; ++cc) {
                int copies = 2;
                if (cc == hole[0]) copies--;
                if (cc == hole[1]) copies--;
                if (copies <= 0) continue;
                for (int c = 0; c < copies; ++c) {
                    double r2_val = cfr(trainer, hole, cc, r1, "", 2, pi0, pi1);
                    if (cur_player == 0)
                        val += r2_val;
                    else
                        val += -r2_val;
                }
            }
            return val / remaining;
        } else {
            // Showdown: utility for cur_player
            int inv[2];
            compute_investments(r1, r2, inv);
            int w = showdown_winner(hole[0], hole[1], community);
            double p0_util = (w == 1) ? inv[1] : (w == -1) ? -inv[0] : 0.0;
            return (cur_player == 0) ? p0_util : -p0_util;
        }
    }

    // ── Decision node ─────────────────────────────────────────────────────
    int player = rr.player;  // == cur_player
    int num_actions = static_cast<int>(rr.actions.size());

    std::string key;
    key += std::to_string(hole[player]);
    if (round == 2) key += std::to_string(community);
    key += ":";
    key += r1;
    if (round == 2) key += "/" + r2;

    InfoSet& info = trainer.get(key, num_actions);
    auto strategy = info.getStrategy();

    std::vector<double> action_util(num_actions, 0.0);
    double node_util = 0;

    for (int a = 0; a < num_actions; ++a) {
        std::string next_r1 = r1, next_r2 = r2;
        if (round == 1) next_r1 += rr.actions[a];
        else            next_r2 += rr.actions[a];

        // Negate on recursion (child returns utility for the other player)
        if (player == 0)
            action_util[a] = -cfr(trainer, hole, community,
                                  next_r1, next_r2, round,
                                  pi0 * strategy[a], pi1);
        else
            action_util[a] = -cfr(trainer, hole, community,
                                  next_r1, next_r2, round,
                                  pi0, pi1 * strategy[a]);

        node_util += strategy[a] * action_util[a];
    }

    // Regret update (no sign needed with current-player convention)
    double cf_reach = (player == 0) ? pi1 : pi0;
    for (int a = 0; a < num_actions; ++a)
        info.cumulative_regret[a] += cf_reach * (action_util[a] - node_util);

    // Strategy sum update
    double reach = (player == 0) ? pi0 : pi1;
    for (int a = 0; a < num_actions; ++a)
        info.cumulative_strategy[a] += reach * strategy[a];

    return node_util;
}

// ── Best response (info-set-constrained) ───────────────────────────────────
// Traverses with a probability distribution over opponent cards.
// At br_player's nodes: picks the best action (same across all opp cards).
// At opponent's nodes: marginalizes over opp cards using their avg strategy.
// Returns: sum over opp of opp_prob[opp] * utility_for_br_player.

static double br_traverse(CFRTrainer& trainer,
                          int br_card, double opp_prob[3],
                          int community,
                          const std::string& r1, const std::string& r2,
                          int round, int br_player) {
    const std::string& cur = (round == 1) ? r1 : r2;
    RoundResult rr = analyse_round(cur);
    double sign = (br_player == 0) ? 1.0 : -1.0;

    // ── Fold terminal ────────────────────────────────────────────────────
    if (rr.done && rr.folded) {
        int inv[2];
        compute_investments(r1, r2, inv);
        double p0_util = (rr.folder == 0) ? -inv[0] : static_cast<double>(inv[1]);
        double total_opp = 0;
        for (int opp = 0; opp < 3; ++opp) total_opp += opp_prob[opp];
        return total_opp * sign * p0_util;
    }

    // ── Round over, not folded ───────────────────────────────────────────
    if (rr.done && !rr.folded) {
        if (round == 1) {
            // Chance node: deal community card
            // For each (opp, cc) pair, prob of cc = copies(cc, br_card, opp) / 4
            double val = 0;
            for (int cc = 0; cc < 3; ++cc) {
                double new_opp_prob[3] = {};
                for (int opp = 0; opp < 3; ++opp) {
                    if (opp_prob[opp] <= 0) continue;
                    int copies = 2;
                    if (cc == br_card) copies--;
                    if (cc == opp) copies--;
                    if (copies <= 0) continue;
                    new_opp_prob[opp] = opp_prob[opp] * copies / 4.0;
                }
                val += br_traverse(trainer, br_card, new_opp_prob, cc,
                                   r1, "", 2, br_player);
            }
            return val;
        } else {
            // Showdown
            int inv[2];
            compute_investments(r1, r2, inv);
            double val = 0;
            for (int opp = 0; opp < 3; ++opp) {
                if (opp_prob[opp] <= 0) continue;
                // showdown_winner takes (P0_card, P1_card, community)
                int h0 = (br_player == 0) ? br_card : opp;
                int h1 = (br_player == 0) ? opp : br_card;
                int w = showdown_winner(h0, h1, community);
                double p0_util = (w == 1) ? inv[1] : (w == -1) ? -inv[0] : 0.0;
                val += opp_prob[opp] * sign * p0_util;
            }
            return val;
        }
    }

    // ── Decision node ────────────────────────────────────────────────────
    int player = rr.player;
    int num_actions = static_cast<int>(rr.actions.size());

    if (player == br_player) {
        // Best response: pick action maximizing expected utility across all opp cards
        double best = -1e18;
        for (int a = 0; a < num_actions; ++a) {
            std::string nr1 = r1, nr2 = r2;
            if (round == 1) nr1 += rr.actions[a];
            else            nr2 += rr.actions[a];
            best = std::max(best, br_traverse(trainer, br_card, opp_prob, community,
                                              nr1, nr2, round, br_player));
        }
        return best;
    } else {
        // Opponent's turn: branch by action, update opp_prob via Bayes
        double total = 0;
        for (int a = 0; a < num_actions; ++a) {
            double new_opp_prob[3] = {};
            for (int opp = 0; opp < 3; ++opp) {
                if (opp_prob[opp] <= 0) continue;
                std::string key;
                key += std::to_string(opp);
                if (round == 2) key += std::to_string(community);
                key += ":";
                key += r1;
                if (round == 2) key += "/" + r2;
                auto avg = trainer.get(key, num_actions).getAverageStrategy();
                new_opp_prob[opp] = opp_prob[opp] * avg[a];
            }
            std::string nr1 = r1, nr2 = r2;
            if (round == 1) nr1 += rr.actions[a];
            else            nr2 += rr.actions[a];
            total += br_traverse(trainer, br_card, new_opp_prob, community,
                                 nr1, nr2, round, br_player);
        }
        return total;
    }
}

static double compute_exploitability(CFRTrainer& trainer) {
    double br_sum[2] = {};
    for (int br_player = 0; br_player < 2; ++br_player) {
        for (int br_card = 0; br_card < 3; ++br_card) {
            // 6-card Leduc: 5 remaining cards after br_card is dealt.
            // Same rank: 1 copy left → prob 1/5
            // Other ranks: 2 copies each → prob 2/5
            double opp_prob[3];
            for (int opp = 0; opp < 3; ++opp)
                opp_prob[opp] = (opp == br_card) ? 1.0 / 5.0 : 2.0 / 5.0;
            br_sum[br_player] += br_traverse(trainer, br_card, opp_prob, -1,
                                             "", "", 1, br_player);
        }
    }
    // Average over 3 br_cards, then average the two players
    return (br_sum[0] / 3.0 + br_sum[1] / 3.0) / 2.0;
}

// ── Strategy evaluation (plays both sides with avg strategy) ────────────────
// Returns P0's expected utility for a specific deal.
static double eval_avg(CFRTrainer& trainer,
                       int hole[2], int community,
                       const std::string& r1, const std::string& r2,
                       int round) {
    const std::string& cur = (round == 1) ? r1 : r2;
    RoundResult rr = analyse_round(cur);

    if (rr.done && rr.folded) {
        int inv[2];
        compute_investments(r1, r2, inv);
        return (rr.folder == 0) ? -inv[0] : inv[1];
    }

    if (rr.done && !rr.folded) {
        if (round == 1) {
            double val = 0;
            int remaining = 0;
            for (int cc = 0; cc < 3; ++cc) {
                int copies = 2;
                if (cc == hole[0]) copies--;
                if (cc == hole[1]) copies--;
                if (copies <= 0) continue;
                remaining += copies;
                val += copies * eval_avg(trainer, hole, cc, r1, "", 2);
            }
            return val / remaining;
        } else {
            int inv[2];
            compute_investments(r1, r2, inv);
            int w = showdown_winner(hole[0], hole[1], community);
            if (w == 1) return inv[1];
            if (w == -1) return -inv[0];
            return 0;
        }
    }

    int player = rr.player;
    int num_actions = static_cast<int>(rr.actions.size());

    std::string key;
    key += std::to_string(hole[player]);
    if (round == 2) key += std::to_string(community);
    key += ":";
    key += r1;
    if (round == 2) key += "/" + r2;

    auto avg = trainer.get(key, num_actions).getAverageStrategy();
    double val = 0;
    for (int a = 0; a < num_actions; ++a) {
        std::string nr1 = r1, nr2 = r2;
        if (round == 1) nr1 += rr.actions[a];
        else            nr2 += rr.actions[a];
        val += avg[a] * eval_avg(trainer, hole, community, nr1, nr2, round);
    }
    return val;
}

// ── Per-deal BR (CHEATING: br_player sees opponent's card) ──────────────────
// This gives an UPPER BOUND on the correct BR value.
static double cheat_br(CFRTrainer& trainer,
                       int hole[2], int community,
                       const std::string& r1, const std::string& r2,
                       int round, int br_player) {
    const std::string& cur = (round == 1) ? r1 : r2;
    RoundResult rr = analyse_round(cur);

    if (rr.done && rr.folded) {
        int inv[2];
        compute_investments(r1, r2, inv);
        double p0_util = (rr.folder == 0) ? -inv[0] : static_cast<double>(inv[1]);
        return (br_player == 0) ? p0_util : -p0_util;
    }

    if (rr.done && !rr.folded) {
        if (round == 1) {
            double val = 0;
            int remaining = 0;
            for (int cc = 0; cc < 3; ++cc) {
                int copies = 2;
                if (cc == hole[0]) copies--;
                if (cc == hole[1]) copies--;
                if (copies <= 0) continue;
                remaining += copies;
                val += copies * cheat_br(trainer, hole, cc, r1, "", 2, br_player);
            }
            return val / remaining;
        } else {
            int inv[2];
            compute_investments(r1, r2, inv);
            int w = showdown_winner(hole[0], hole[1], community);
            double p0_util = (w == 1) ? inv[1] : (w == -1) ? -inv[0] : 0.0;
            return (br_player == 0) ? p0_util : -p0_util;
        }
    }

    int player = rr.player;
    int num_actions = static_cast<int>(rr.actions.size());

    std::string key;
    key += std::to_string(hole[player]);
    if (round == 2) key += std::to_string(community);
    key += ":";
    key += r1;
    if (round == 2) key += "/" + r2;

    if (player == br_player) {
        double best = -1e9;
        for (int a = 0; a < num_actions; ++a) {
            std::string nr1 = r1, nr2 = r2;
            if (round == 1) nr1 += rr.actions[a];
            else            nr2 += rr.actions[a];
            best = std::max(best, cheat_br(trainer, hole, community,
                                           nr1, nr2, round, br_player));
        }
        return best;
    } else {
        auto avg = trainer.get(key, num_actions).getAverageStrategy();
        double val = 0;
        for (int a = 0; a < num_actions; ++a) {
            std::string nr1 = r1, nr2 = r2;
            if (round == 1) nr1 += rr.actions[a];
            else            nr2 += rr.actions[a];
            val += avg[a] * cheat_br(trainer, hole, community,
                                     nr1, nr2, round, br_player);
        }
        return val;
    }
}

// ── Diagnostic: traverse with opp_prob but NO best response ─────────────────
// Both players use average strategy. Should give P0's game value.
static double no_br_traverse(CFRTrainer& trainer,
                             int br_card, double opp_prob[3],
                             int community,
                             const std::string& r1, const std::string& r2,
                             int round, int br_player) {
    const std::string& cur = (round == 1) ? r1 : r2;
    RoundResult rr = analyse_round(cur);
    double sign = (br_player == 0) ? 1.0 : -1.0;

    if (rr.done && rr.folded) {
        int inv[2];
        compute_investments(r1, r2, inv);
        double p0_util = (rr.folder == 0) ? -inv[0] : static_cast<double>(inv[1]);
        double total_opp = 0;
        for (int opp = 0; opp < 3; ++opp) total_opp += opp_prob[opp];
        return total_opp * sign * p0_util;
    }

    if (rr.done && !rr.folded) {
        if (round == 1) {
            double val = 0;
            for (int cc = 0; cc < 3; ++cc) {
                double new_opp_prob[3] = {};
                for (int opp = 0; opp < 3; ++opp) {
                    if (opp_prob[opp] <= 0) continue;
                    int copies = 2;
                    if (cc == br_card) copies--;
                    if (cc == opp) copies--;
                    if (copies <= 0) continue;
                    new_opp_prob[opp] = opp_prob[opp] * copies / 4.0;
                }
                val += no_br_traverse(trainer, br_card, new_opp_prob, cc,
                                      r1, "", 2, br_player);
            }
            return val;
        } else {
            int inv[2];
            compute_investments(r1, r2, inv);
            double val = 0;
            for (int opp = 0; opp < 3; ++opp) {
                if (opp_prob[opp] <= 0) continue;
                int h0 = (br_player == 0) ? br_card : opp;
                int h1 = (br_player == 0) ? opp : br_card;
                int w = showdown_winner(h0, h1, community);
                double p0_util = (w == 1) ? inv[1] : (w == -1) ? -inv[0] : 0.0;
                val += opp_prob[opp] * sign * p0_util;
            }
            return val;
        }
    }

    // Decision node: BOTH players use average strategy (no max)
    int player = rr.player;
    int num_actions = static_cast<int>(rr.actions.size());

    // All players marginalize — use the acting player's average strategy
    double total = 0;
    for (int a = 0; a < num_actions; ++a) {
        double new_opp_prob[3] = {};
        if (player != br_player) {
            // Opponent's node: update opp_prob via opponent's avg strategy
            for (int opp = 0; opp < 3; ++opp) {
                if (opp_prob[opp] <= 0) continue;
                std::string key;
                key += std::to_string(opp);
                if (round == 2) key += std::to_string(community);
                key += ":";
                key += r1;
                if (round == 2) key += "/" + r2;
                auto avg = trainer.get(key, num_actions).getAverageStrategy();
                new_opp_prob[opp] = opp_prob[opp] * avg[a];
            }
        } else {
            // br_player's node: use br_player's avg strategy, opp_prob unchanged
            std::string key;
            key += std::to_string(br_card);
            if (round == 2) key += std::to_string(community);
            key += ":";
            key += r1;
            if (round == 2) key += "/" + r2;
            auto avg = trainer.get(key, num_actions).getAverageStrategy();
            for (int opp = 0; opp < 3; ++opp)
                new_opp_prob[opp] = opp_prob[opp] * avg[a];
        }
        std::string nr1 = r1, nr2 = r2;
        if (round == 1) nr1 += rr.actions[a];
        else            nr2 += rr.actions[a];
        total += no_br_traverse(trainer, br_card, new_opp_prob, community,
                                nr1, nr2, round, br_player);
    }
    return total;
}

// ── Tests ──────────────────────────────────────────────────────────────────

TEST(Leduc, ConvergesToLowExploitability) {
    CFRTrainer trainer;
    constexpr int ITERS = 100'000;

    for (int t = 0; t < ITERS; ++t) {
        for (int h0 = 0; h0 < 3; ++h0)
            for (int h1 = 0; h1 < 3; ++h1) {
                // In 6-card Leduc, same-rank deals are valid (different suits).
                // Weight: different-rank = 4 combos (2x2 suits), same-rank = 2 combos.
                int weight = (h0 == h1) ? 2 : 4;
                int hole[2] = {h0, h1};
                for (int w = 0; w < weight; ++w)
                    cfr(trainer, hole, -1, "", "", 1, 1.0, 1.0);
            }
    }

    // Extract BR policy from br_traverse, then evaluate it independently.
    // Step 1: Modified br_traverse that records which action was chosen at each
    //         br_player info set.
    // Step 2: eval function that uses the BR policy for br_player, avg for opponent.
    // If step 2 matches br_traverse, br_traverse is correct.

    std::unordered_map<std::string, int> br_policy;  // info_set_key -> action index

    // br_traverse that records policy
    std::function<double(CFRTrainer&, int, double[3], int,
                         const std::string&, const std::string&,
                         int, int)> br_record;
    br_record = [&](CFRTrainer& tr, int br_card, double op[3], int community,
                    const std::string& r1, const std::string& r2,
                    int round, int br_player) -> double {
        const std::string& cur = (round == 1) ? r1 : r2;
        RoundResult rr = analyse_round(cur);
        double sign = (br_player == 0) ? 1.0 : -1.0;

        if (rr.done && rr.folded) {
            int inv[2]; compute_investments(r1, r2, inv);
            double p0u = (rr.folder == 0) ? -inv[0] : static_cast<double>(inv[1]);
            double tot = 0; for (int o=0;o<3;++o) tot += op[o];
            return tot * sign * p0u;
        }
        if (rr.done && !rr.folded) {
            if (round == 1) {
                double val = 0;
                for (int cc = 0; cc < 3; ++cc) {
                    double np[3] = {};
                    for (int opp = 0; opp < 3; ++opp) {
                        if (op[opp] <= 0) continue;
                        int copies = 2-(cc==br_card)-(cc==opp);
                        if (copies <= 0) continue;
                        np[opp] = op[opp] * copies / 4.0;
                    }
                    val += br_record(tr, br_card, np, cc, r1, "", 2, br_player);
                }
                return val;
            } else {
                int inv[2]; compute_investments(r1, r2, inv);
                double val = 0;
                for (int opp = 0; opp < 3; ++opp) {
                    if (op[opp] <= 0) continue;
                    int h0=(br_player==0)?br_card:opp, h1=(br_player==0)?opp:br_card;
                    int w = showdown_winner(h0, h1, community);
                    double p0u = (w==1)?inv[1]:(w==-1)?-inv[0]:0.0;
                    val += op[opp] * sign * p0u;
                }
                return val;
            }
        }

        int player = rr.player;
        int na = static_cast<int>(rr.actions.size());

        // Build info set key for acting player
        std::string key;
        if (player == br_player) {
            key += std::to_string(br_card);
        } else {
            // Opponent - we don't record their policy
        }

        if (player == br_player) {
            if (round == 2) key += std::to_string(community);
            key += ":"; key += r1;
            if (round == 2) key += "/" + r2;

            double best = -1e18;
            int best_a = 0;
            for (int a = 0; a < na; ++a) {
                std::string nr1=r1, nr2=r2;
                if(round==1) nr1+=rr.actions[a]; else nr2+=rr.actions[a];
                double v = br_record(tr, br_card, op, community, nr1, nr2, round, br_player);
                if (v > best) { best = v; best_a = a; }
            }
            br_policy[key] = best_a;
            return best;
        } else {
            double total = 0;
            for (int a = 0; a < na; ++a) {
                double np[3] = {};
                for (int opp = 0; opp < 3; ++opp) {
                    if (op[opp] <= 0) continue;
                    std::string okey = std::to_string(opp);
                    if (round == 2) okey += std::to_string(community);
                    okey += ":"; okey += r1;
                    if (round == 2) okey += "/" + r2;
                    auto avg = tr.get(okey, na).getAverageStrategy();
                    np[opp] = op[opp] * avg[a];
                }
                std::string nr1=r1, nr2=r2;
                if(round==1) nr1+=rr.actions[a]; else nr2+=rr.actions[a];
                total += br_record(tr, br_card, np, community, nr1, nr2, round, br_player);
            }
            return total;
        }
    };

    // eval_br: evaluate a specific deal using br_policy for br_player, avg for opponent
    // Returns P0's utility (like eval_avg)
    std::function<double(CFRTrainer&, int[2], int,
                         const std::string&, const std::string&,
                         int, int)> eval_br;
    eval_br = [&](CFRTrainer& tr, int hole[2], int community,
                  const std::string& r1, const std::string& r2,
                  int round, int br_player) -> double {
        const std::string& cur = (round == 1) ? r1 : r2;
        RoundResult rr = analyse_round(cur);

        if (rr.done && rr.folded) {
            int inv[2]; compute_investments(r1, r2, inv);
            return (rr.folder == 0) ? -inv[0] : static_cast<double>(inv[1]);
        }
        if (rr.done && !rr.folded) {
            if (round == 1) {
                double val = 0; int remaining = 0;
                for (int cc = 0; cc < 3; ++cc) {
                    int copies = 2-(cc==hole[0])-(cc==hole[1]);
                    if (copies <= 0) continue;
                    remaining += copies;
                    val += copies * eval_br(tr, hole, cc, r1, "", 2, br_player);
                }
                return val / remaining;
            } else {
                int inv[2]; compute_investments(r1, r2, inv);
                int w = showdown_winner(hole[0], hole[1], community);
                if (w == 1) return static_cast<double>(inv[1]);
                if (w == -1) return static_cast<double>(-inv[0]);
                return 0.0;
            }
        }

        int player = rr.player;
        int na = static_cast<int>(rr.actions.size());

        std::string key;
        key += std::to_string(hole[player]);
        if (round == 2) key += std::to_string(community);
        key += ":"; key += r1;
        if (round == 2) key += "/" + r2;

        if (player == br_player) {
            // Use BR policy
            auto it = br_policy.find(key);
            if (it == br_policy.end()) {
                // Policy not recorded - use avg strategy as fallback
                auto avg = tr.get(key, na).getAverageStrategy();
                double val = 0;
                for (int a = 0; a < na; ++a) {
                    std::string nr1=r1, nr2=r2;
                    if(round==1) nr1+=rr.actions[a]; else nr2+=rr.actions[a];
                    val += avg[a] * eval_br(tr, hole, community, nr1, nr2, round, br_player);
                }
                return val;
            }
            int best_a = it->second;
            std::string nr1=r1, nr2=r2;
            if(round==1) nr1+=rr.actions[best_a]; else nr2+=rr.actions[best_a];
            return eval_br(tr, hole, community, nr1, nr2, round, br_player);
        } else {
            // Opponent uses avg strategy
            auto avg = tr.get(key, na).getAverageStrategy();
            double val = 0;
            for (int a = 0; a < na; ++a) {
                std::string nr1=r1, nr2=r2;
                if(round==1) nr1+=rr.actions[a]; else nr2+=rr.actions[a];
                val += avg[a] * eval_br(tr, hole, community, nr1, nr2, round, br_player);
            }
            return val;
        }
    };

    std::cout << "\n--- BR policy evaluation vs br_traverse ---\n";
    for (int bp = 0; bp < 2; ++bp) {
        double br_trav_sum = 0;
        double br_eval_sum = 0;
        for (int bc = 0; bc < 3; ++bc) {
            // Step 1: Run br_record to get br_traverse value AND record policy
            br_policy.clear();
            double op[3]; for(int o=0;o<3;++o) op[o]=(o==bc)?1.0/5.0:2.0/5.0;
            double trav_val = br_record(trainer, bc, op, -1, "", "", 1, bp);
            br_trav_sum += trav_val;

            // Step 2: Evaluate the recorded policy using per-deal eval_br
            double eval_val = 0;
            for (int opp = 0; opp < 3; ++opp) {
                double opp_w = (opp == bc) ? 1.0 / 5.0 : 2.0 / 5.0;
                int hole[2];
                hole[bp] = bc;
                hole[1-bp] = opp;
                double p0u = eval_br(trainer, hole, -1, "", "", 1, bp);
                // Convert to br_player's utility
                double bpu = (bp == 0) ? p0u : -p0u;
                eval_val += opp_w * bpu;
            }
            br_eval_sum += eval_val;

            if (std::abs(trav_val - eval_val) > 0.001) {
                std::cout << "DIFF bp=" << bp << " bc=" << bc
                          << " traverse=" << trav_val << " eval=" << eval_val
                          << " diff=" << (trav_val - eval_val) << "\n";
            }
        }
        std::cout << "bp=" << bp << " traverse_sum/3=" << br_trav_sum/3.0
                  << " eval_sum/3=" << br_eval_sum/3.0 << "\n";
    }

    double exploit = compute_exploitability(trainer);
    EXPECT_LT(exploit, 0.05)
        << "Exploitability after " << ITERS << " iterations: " << exploit;
}

// ── 1-Round Leduc (no community card) ───────────────────────────────────────
// Same as Leduc but showdown after round 1 (no round 2, no community card).
// Higher card wins. This isolates the multi-round handling.

static double cfr_1round(CFRTrainer& trainer,
                         int hole[2],
                         const std::string& r1,
                         double pi0, double pi1) {
    RoundResult rr = analyse_round(r1);
    int cur_player = static_cast<int>(r1.size()) % 2;

    if (rr.done && rr.folded) {
        int inv[2] = {1, 1};
        auto add_round = [&](const std::string& hist, int bet_size) {
            int p = 0;
            for (char ch : hist) {
                int who = p % 2;
                if (ch == 'b') inv[who] += bet_size;
                else if (ch == 'c') { inv[who] += inv[1-who] - inv[who]; }
                else if (ch == 'r') { int d = inv[1-who]-inv[who]; inv[who] += d + bet_size; }
                ++p;
            }
        };
        add_round(r1, 2);
        if (rr.folder == 0)
            return (cur_player == 0) ? -inv[0] : inv[0];
        else
            return (cur_player == 0) ? inv[1] : -inv[1];
    }

    if (rr.done && !rr.folded) {
        // Immediate showdown (no community card)
        int inv[2] = {1, 1};
        auto add_round = [&](const std::string& hist, int bet_size) {
            int p = 0;
            for (char ch : hist) {
                int who = p % 2;
                if (ch == 'b') inv[who] += bet_size;
                else if (ch == 'c') { inv[who] += inv[1-who] - inv[who]; }
                else if (ch == 'r') { int d = inv[1-who]-inv[who]; inv[who] += d + bet_size; }
                ++p;
            }
        };
        add_round(r1, 2);
        // Higher card wins
        double p0_util = (hole[0] > hole[1]) ? inv[1] :
                         (hole[0] < hole[1]) ? -inv[0] : 0.0;
        return (cur_player == 0) ? p0_util : -p0_util;
    }

    int player = rr.player;
    int num_actions = static_cast<int>(rr.actions.size());

    std::string key = std::to_string(hole[player]) + ":" + r1;

    InfoSet& info = trainer.get(key, num_actions);
    auto strategy = info.getStrategy();

    std::vector<double> action_util(num_actions, 0.0);
    double node_util = 0;
    for (int a = 0; a < num_actions; ++a) {
        std::string next = r1 + rr.actions[a];
        if (player == 0)
            action_util[a] = -cfr_1round(trainer, hole, next, pi0*strategy[a], pi1);
        else
            action_util[a] = -cfr_1round(trainer, hole, next, pi0, pi1*strategy[a]);
        node_util += strategy[a] * action_util[a];
    }

    double cf_reach = (player == 0) ? pi1 : pi0;
    for (int a = 0; a < num_actions; ++a)
        info.cumulative_regret[a] += cf_reach * (action_util[a] - node_util);

    double reach = (player == 0) ? pi0 : pi1;
    for (int a = 0; a < num_actions; ++a)
        info.cumulative_strategy[a] += reach * strategy[a];

    return node_util;
}

static double br_1round(CFRTrainer& trainer,
                        int br_card, double opp_prob[3],
                        const std::string& r1, int br_player) {
    RoundResult rr = analyse_round(r1);
    double sign = (br_player == 0) ? 1.0 : -1.0;

    if (rr.done && rr.folded) {
        int inv[2] = {1, 1};
        auto add = [&](const std::string& h, int bs) {
            int p=0; for(char c:h) { int w=p%2;
                if(c=='b') inv[w]+=bs; else if(c=='c'){inv[w]+=inv[1-w]-inv[w];}
                else if(c=='r'){int d=inv[1-w]-inv[w];inv[w]+=d+bs;} ++p; }
        };
        add(r1, 2);
        double p0u = (rr.folder==0) ? -inv[0] : static_cast<double>(inv[1]);
        double tot = 0; for(int o=0;o<3;++o) tot += opp_prob[o];
        return tot * sign * p0u;
    }

    if (rr.done && !rr.folded) {
        int inv[2] = {1, 1};
        auto add = [&](const std::string& h, int bs) {
            int p=0; for(char c:h) { int w=p%2;
                if(c=='b') inv[w]+=bs; else if(c=='c'){inv[w]+=inv[1-w]-inv[w];}
                else if(c=='r'){int d=inv[1-w]-inv[w];inv[w]+=d+bs;} ++p; }
        };
        add(r1, 2);
        double val = 0;
        for (int opp = 0; opp < 3; ++opp) {
            if (opp_prob[opp] <= 0) continue;
            int h0 = (br_player==0) ? br_card : opp;
            int h1 = (br_player==0) ? opp : br_card;
            double p0u = (h0>h1) ? inv[1] : (h0<h1) ? -inv[0] : 0.0;
            val += opp_prob[opp] * sign * p0u;
        }
        return val;
    }

    int player = rr.player;
    int num_actions = static_cast<int>(rr.actions.size());

    if (player == br_player) {
        double best = -1e18;
        for (int a = 0; a < num_actions; ++a) {
            std::string next = r1 + rr.actions[a];
            best = std::max(best, br_1round(trainer, br_card, opp_prob, next, br_player));
        }
        return best;
    } else {
        double total = 0;
        for (int a = 0; a < num_actions; ++a) {
            double new_opp[3] = {};
            for (int opp = 0; opp < 3; ++opp) {
                if (opp_prob[opp] <= 0) continue;
                std::string key = std::to_string(opp) + ":" + r1;
                auto avg = trainer.get(key, num_actions).getAverageStrategy();
                new_opp[opp] = opp_prob[opp] * avg[a];
            }
            std::string next = r1 + rr.actions[a];
            total += br_1round(trainer, br_card, new_opp, next, br_player);
        }
        return total;
    }
}

// ── 2-round but NO round 2 betting (immediate showdown after community) ─────
static double cfr_2round_nob(CFRTrainer& trainer,
                             int hole[2], int community,
                             const std::string& r1,
                             double pi0, double pi1) {
    RoundResult rr = analyse_round(r1);
    int cur_player = static_cast<int>(r1.size()) % 2;

    if (rr.done && rr.folded) {
        int inv[2] = {1, 1};
        int p=0; for(char c:r1) { int w=p%2;
            if(c=='b') inv[w]+=2; else if(c=='c'){inv[w]+=inv[1-w]-inv[w];}
            else if(c=='r'){int d=inv[1-w]-inv[w];inv[w]+=d+2;} ++p; }
        if (rr.folder == 0)
            return (cur_player==0) ? -inv[0] : inv[0];
        else
            return (cur_player==0) ? inv[1] : -inv[1];
    }

    if (rr.done && !rr.folded) {
        // Chance node: deal community, immediate showdown
        int inv[2] = {1, 1};
        int p=0; for(char c:r1) { int w=p%2;
            if(c=='b') inv[w]+=2; else if(c=='c'){inv[w]+=inv[1-w]-inv[w];}
            else if(c=='r'){int d=inv[1-w]-inv[w];inv[w]+=d+2;} ++p; }

        double val = 0;
        int remaining = 0;
        for (int cc = 0; cc < 3; ++cc) {
            int copies = 2;
            if (cc == hole[0]) copies--;
            if (cc == hole[1]) copies--;
            if (copies <= 0) continue;
            remaining += copies;
            int w = showdown_winner(hole[0], hole[1], cc);
            double p0u = (w==1) ? inv[1] : (w==-1) ? -inv[0] : 0.0;
            double u = (cur_player==0) ? p0u : -p0u;
            val += copies * u;
        }
        return val / remaining;
    }

    int player = rr.player;
    int num_actions = static_cast<int>(rr.actions.size());
    std::string key = std::to_string(hole[player]) + ":" + r1;

    InfoSet& info = trainer.get(key, num_actions);
    auto strategy = info.getStrategy();
    std::vector<double> au(num_actions, 0.0);
    double nu = 0;
    for (int a = 0; a < num_actions; ++a) {
        std::string next = r1 + rr.actions[a];
        if (player == 0)
            au[a] = -cfr_2round_nob(trainer, hole, -1, next, pi0*strategy[a], pi1);
        else
            au[a] = -cfr_2round_nob(trainer, hole, -1, next, pi0, pi1*strategy[a]);
        nu += strategy[a] * au[a];
    }
    double cf = (player==0) ? pi1 : pi0;
    for (int a=0; a<num_actions; ++a)
        info.cumulative_regret[a] += cf * (au[a] - nu);
    double rch = (player==0) ? pi0 : pi1;
    for (int a=0; a<num_actions; ++a)
        info.cumulative_strategy[a] += rch * strategy[a];
    return nu;
}

static double br_2round_nob(CFRTrainer& trainer,
                            int br_card, double opp_prob[3],
                            const std::string& r1, int br_player) {
    RoundResult rr = analyse_round(r1);
    double sign = (br_player==0) ? 1.0 : -1.0;

    if (rr.done && rr.folded) {
        int inv[2] = {1,1};
        int p=0; for(char c:r1) { int w=p%2;
            if(c=='b') inv[w]+=2; else if(c=='c'){inv[w]+=inv[1-w]-inv[w];}
            else if(c=='r'){int d=inv[1-w]-inv[w];inv[w]+=d+2;} ++p; }
        double p0u = (rr.folder==0) ? -inv[0] : static_cast<double>(inv[1]);
        double tot=0; for(int o=0;o<3;++o) tot+=opp_prob[o];
        return tot * sign * p0u;
    }

    if (rr.done && !rr.folded) {
        // Chance node + immediate showdown
        int inv[2] = {1,1};
        int p=0; for(char c:r1) { int w=p%2;
            if(c=='b') inv[w]+=2; else if(c=='c'){inv[w]+=inv[1-w]-inv[w];}
            else if(c=='r'){int d=inv[1-w]-inv[w];inv[w]+=d+2;} ++p; }

        double val = 0;
        for (int cc = 0; cc < 3; ++cc) {
            for (int opp = 0; opp < 3; ++opp) {
                if (opp_prob[opp] <= 0) continue;
                int copies = 2;
                if (cc == br_card) copies--;
                if (cc == opp) copies--;
                if (copies <= 0) continue;
                int h0 = (br_player==0) ? br_card : opp;
                int h1 = (br_player==0) ? opp : br_card;
                int w = showdown_winner(h0, h1, cc);
                double p0u = (w==1) ? inv[1] : (w==-1) ? -inv[0] : 0.0;
                val += opp_prob[opp] * (static_cast<double>(copies)/4.0) * sign * p0u;
            }
        }
        return val;
    }

    int player = rr.player;
    int num_actions = static_cast<int>(rr.actions.size());

    if (player == br_player) {
        double best = -1e18;
        for (int a = 0; a < num_actions; ++a) {
            std::string next = r1 + rr.actions[a];
            best = std::max(best, br_2round_nob(trainer, br_card, opp_prob, next, br_player));
        }
        return best;
    } else {
        double total = 0;
        for (int a = 0; a < num_actions; ++a) {
            double newop[3] = {};
            for (int opp=0; opp<3; ++opp) {
                if (opp_prob[opp] <= 0) continue;
                std::string key = std::to_string(opp) + ":" + r1;
                auto avg = trainer.get(key, num_actions).getAverageStrategy();
                newop[opp] = opp_prob[opp] * avg[a];
            }
            std::string next = r1 + rr.actions[a];
            total += br_2round_nob(trainer, br_card, newop, next, br_player);
        }
        return total;
    }
}

TEST(Leduc, TwoRoundNoBettingConverges) {
    CFRTrainer trainer;
    for (int t = 0; t < 100'000; ++t)
        for (int h0=0;h0<3;++h0) for (int h1=0;h1<3;++h1) {
            if (h0==h1) continue;
            int hole[2]={h0,h1};
            cfr_2round_nob(trainer, hole, -1, "", 1.0, 1.0);
        }
    double bs[2]={};
    for (int bp=0;bp<2;++bp) for (int bc=0;bc<3;++bc) {
        double op[3]; for(int o=0;o<3;++o) op[o]=(o==bc)?0.0:0.5;
        bs[bp] += br_2round_nob(trainer, bc, op, "", bp);
    }
    double e = (bs[0]/3.0+bs[1]/3.0)/2.0;
    std::cout << "2round-nob exploit=" << e << "\n";
    EXPECT_LT(e, 0.01);
}

TEST(Leduc, OneRoundConverges) {
    CFRTrainer trainer;
    constexpr int ITERS = 100'000;
    for (int t = 0; t < ITERS; ++t) {
        for (int h0 = 0; h0 < 3; ++h0)
            for (int h1 = 0; h1 < 3; ++h1) {
                if (h0 == h1) continue;
                int hole[2] = {h0, h1};
                cfr_1round(trainer, hole, "", 1.0, 1.0);
            }
    }
    double br_sum[2] = {};
    for (int bp = 0; bp < 2; ++bp)
        for (int bc = 0; bc < 3; ++bc) {
            double op[3];
            for (int o = 0; o < 3; ++o) op[o] = (o==bc) ? 0.0 : 0.5;
            br_sum[bp] += br_1round(trainer, bc, op, "", bp);
        }
    double exploit = (br_sum[0]/3.0 + br_sum[1]/3.0) / 2.0;
    std::cout << "1-round exploit=" << exploit << "\n";
    EXPECT_LT(exploit, 0.01);
}
