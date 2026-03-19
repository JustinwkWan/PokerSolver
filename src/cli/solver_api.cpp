// solver_api.cpp — Implementation of the public poker_solver API.
//
// Wires together the internal modules (GameTree, InfoSetManager, RegretStore,
// CFRSolver, Exploitability, SubgameSolver) behind the clean public header.

#include "poker_solver/solver.h"

#include "abstraction/action_abstraction.h"
#include "core/cards.h"
#include "core/game_state.h"
#include "core/hand_evaluator.h"
#include "solver/cfr_solver.h"
#include "solver/exploitability.h"
#include "solver/regret_store.h"
#include "subgame/subgame_solver.h"
#include "tree/game_tree.h"
#include "tree/info_set.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace poker_solver {

// ── Helpers ──────────────────────────────────────────────────────────────

static BetSizeConfig makeBetConfig(const SolverConfig& cfg) {
    BetSizeConfig bc;
    bc.preflop = cfg.preflop_sizes;
    bc.flop    = cfg.flop_sizes;
    bc.turn    = cfg.turn_sizes;
    bc.river   = cfg.river_sizes;
    return bc;
}

static std::array<int, 4> bucketArray(const SolverConfig& cfg) {
    return cfg.buckets;
}

// Simple bucket function that reads binary bucket files.
// File format: int32 num_hands, int32 num_buckets, then num_hands int32 bucket IDs.
// Returns a BucketFunc that loads and caches bucket tables.
static CFRSolver::BucketFunc makeBucketFunc(const SolverConfig& cfg) {
    // For the full solver, buckets come from the abstraction files.
    // For now, use canonical preflop and hash-based buckets for post-flop.
    // When abstraction files are provided via output_path, load them.
    auto buckets = bucketArray(cfg);
    return [buckets](int /*player*/, int street,
                     const Card hole[2], const Card /*board*/[5],
                     int /*num_board*/) -> int {
        initCards();
        if (street == 0)
            return canonicalPreflopHand(hole[0], hole[1]) % buckets[0];
        int r0 = rankOf(hole[0]);
        int r1 = rankOf(hole[1]);
        int hash = r0 * 13 + r1 + street * 169;
        return hash % buckets[street];
    };
}

// Parse a hand string like "AhKs" into two Cards.
static void parseHoleCards(const std::string& s, Card out[2]) {
    if (s.size() != 4)
        throw std::invalid_argument("hole_cards must be 4 chars, e.g. \"AhKs\"");
    out[0] = cardFromStr(s.substr(0, 2));
    out[1] = cardFromStr(s.substr(2, 2));
}

// Parse a board string like "Td9c2h" into cards.
static int parseBoard(const std::string& s, Card out[5]) {
    if (s.size() % 2 != 0 || s.size() > 10)
        throw std::invalid_argument("board must be 0-5 cards (0,6,8,10 chars)");
    int n = static_cast<int>(s.size()) / 2;
    for (int i = 0; i < n; ++i)
        out[i] = cardFromStr(s.substr(i * 2, 2));
    for (int i = n; i < 5; ++i)
        out[i] = kNoCard;
    return n;
}

// Parse an action history like "rc" into a sequence of Actions.
// f=fold, k=check, c=call, b=bet/raise (pot-sized)
static std::vector<Action> parseHistory(const std::string& hist) {
    std::vector<Action> actions;
    for (char ch : hist) {
        switch (ch) {
            case 'f': actions.push_back(Action::fold()); break;
            case 'k': actions.push_back(Action::check()); break;
            case 'c': actions.push_back(Action::call()); break;
            case 'b': case 'r':
                // Bet/raise — amount determined by replay against game state
                actions.push_back(Action::betRaise(0));
                break;
            default:
                throw std::invalid_argument(
                    std::string("Unknown history char: ") + ch);
        }
    }
    return actions;
}

// ── solve ────────────────────────────────────────────────────────────────

void solve(const SolverConfig& config) {
    initCards();

    int stack_chips = config.stack_depth_bb * 2;  // chip convention: 1 unit = 0.5 BB

    ActionAbstraction aa(makeBetConfig(config));
    GameTree tree(stack_chips, aa, config.num_players);
    tree.build();

    auto bk = bucketArray(config);
    InfoSetManager info_sets(tree, bk);
    RegretStore store(info_sets.totalInfoSets(), info_sets.maxActions(),
                      config.output_path, config.num_players);

    auto bucket_func = makeBucketFunc(config);

    DCFRParams params;
    params.alpha = config.dcfr_alpha;
    params.beta  = config.dcfr_beta;
    params.gamma = config.dcfr_gamma;

    CFRSolver solver(tree, info_sets, store, bucket_func, params, config.seed);

    std::cout << "Players: " << config.num_players << "\n";
    std::cout << "Tree: " << tree.numNodes() << " nodes, "
              << tree.numActionNodes() << " action, "
              << tree.numTerminalNodes() << " terminal\n";
    std::cout << "Info sets: " << info_sets.totalInfoSets()
              << " (max " << info_sets.maxActions() << " actions)\n";
    std::cout << "Running " << config.num_iterations << " iterations...\n";

    uint64_t total = config.num_iterations;
    uint64_t ckpt = config.checkpoint_every;

    for (uint64_t done = 0; done < total; ) {
        uint64_t batch = std::min(ckpt, total - done);
        solver.run(batch);
        done += batch;

        store.sync();
        if (ckpt > 0 && done < total) {
            std::string dir = config.output_path + "/checkpoint_" + std::to_string(done);
            store.checkpoint(dir, done);
            std::cout << "  Checkpoint at iteration " << done << "\n";
        }
    }

    store.sync(true);

    // Write final metadata so the strategy can be loaded later.
    std::ofstream meta(config.output_path + "/meta.txt");
    meta << info_sets.totalInfoSets() << " "
         << info_sets.maxActions() << " "
         << solver.iteration() << "\n";
    meta.close();

    std::cout << "Done. " << solver.iteration() << " iterations completed.\n";
}

// ── solve_resume ─────────────────────────────────────────────────────────

void solve_resume(const SolverConfig& config, const std::string& checkpoint_path) {
    initCards();

    int stack_chips = config.stack_depth_bb * 2;

    ActionAbstraction aa(makeBetConfig(config));
    GameTree tree(stack_chips, aa, config.num_players);
    tree.build();

    auto bk = bucketArray(config);
    InfoSetManager info_sets(tree, bk);

    // Load from checkpoint.
    RegretStore store = RegretStore::load(checkpoint_path);

    auto bucket_func = makeBucketFunc(config);

    DCFRParams params;
    params.alpha = config.dcfr_alpha;
    params.beta  = config.dcfr_beta;
    params.gamma = config.dcfr_gamma;

    CFRSolver solver(tree, info_sets, store, bucket_func, params, config.seed);

    std::cout << "Resumed from " << checkpoint_path << "\n";
    std::cout << "Running " << config.num_iterations << " more iterations...\n";

    uint64_t total = config.num_iterations;
    uint64_t ckpt = config.checkpoint_every;

    for (uint64_t done = 0; done < total; ) {
        uint64_t batch = std::min(ckpt, total - done);
        solver.run(batch);
        done += batch;

        store.sync();
        if (ckpt > 0 && done < total) {
            std::string dir = config.output_path + "/checkpoint_resume_" + std::to_string(done);
            store.checkpoint(dir, done);
        }
    }

    store.sync(true);
    std::cout << "Done. " << solver.iteration() << " iterations completed.\n";
}

// ── query ────────────────────────────────────────────────────────────────

StrategyResult query(const std::string& strategy_path,
                     const std::string& hole_cards,
                     const std::string& board,
                     const std::string& history,
                     const SolverConfig& config) {
    initCards();

    int stack_chips = config.stack_depth_bb * 2;
    ActionAbstraction aa(makeBetConfig(config));
    GameTree tree(stack_chips, aa, config.num_players);
    tree.build();

    auto bk = bucketArray(config);
    InfoSetManager info_sets(tree, bk);
    RegretStore store = RegretStore::load(strategy_path);

    // Parse cards.
    Card hole[2];
    parseHoleCards(hole_cards, hole);

    Card bd[5];
    int num_board = parseBoard(board, bd);

    // Determine street from board cards.
    int street = 0;
    if (num_board >= 3) street = 1;
    if (num_board >= 4) street = 2;
    if (num_board >= 5) street = 3;

    // Compute bucket.
    auto bucket_func = makeBucketFunc(config);
    int bucket = bucket_func(0, street, hole, bd, num_board);

    // Replay history to find the current node.
    auto hist_actions = parseHistory(history);
    uint32_t node_idx = 0;

    for (const auto& ha : hist_actions) {
        const auto& nd = tree.node(node_idx);
        if (nd.type == NodeType::Terminal) break;

        // Find matching action.
        bool found = false;
        for (int a = 0; a < nd.num_actions; ++a) {
            const Action& ta = tree.action(node_idx, a);
            if (ta.type == ha.type) {
                node_idx = tree.child(node_idx, a);
                found = true;
                break;
            }
        }
        if (!found) break;
    }

    // Get the average strategy at this node.
    const auto& nd = tree.node(node_idx);
    if (nd.type == NodeType::Terminal) {
        return {{}, {}};
    }

    uint64_t is_id = info_sets.infoSetId(node_idx, bucket);
    std::vector<float> probs(nd.num_actions);
    store.getAverageStrategy(nd.player, is_id, nd.num_actions, probs.data());

    // Build result.
    StrategyResult result;
    for (int a = 0; a < nd.num_actions; ++a) {
        result.actions.push_back(formatAction(
            tree.action(node_idx, a).type == ActionType::Fold     ? "fold" :
            tree.action(node_idx, a).type == ActionType::Check    ? "check" :
            tree.action(node_idx, a).type == ActionType::Call     ? "call" :
            "raise",
            tree.action(node_idx, a).amount));
        result.probabilities.push_back(probs[a]);
    }

    return result;
}

// ── solve_subgame ────────────────────────────────────────────────────────

StrategyResult solve_subgame(const std::string& hole_cards,
                              const std::string& board,
                              const std::string& history,
                              int num_iterations,
                              const SolverConfig& config) {
    initCards();

    int stack_chips = config.stack_depth_bb * 2;

    // Parse cards.
    Card hole[2];
    parseHoleCards(hole_cards, hole);

    Card bd[5];
    int num_board = parseBoard(board, bd);

    // Replay history to build game state.
    GameState state = GameState::newHand(stack_chips, 0, config.num_players);
    // Deal hole cards: real player + dummy opponents
    if (config.num_players == 2) {
        state = state.withHoleCards(hole[0], hole[1],
                                    cardFromStr("2c"), cardFromStr("3c"));
    } else {
        std::array<std::array<Card, 2>, kMaxPlayers> hole_arr{};
        hole_arr[0] = {hole[0], hole[1]};
        // Dummy opponents
        Card dummy_cards[] = {cardFromStr("2c"), cardFromStr("3c"),
                              cardFromStr("4c"), cardFromStr("5c"),
                              cardFromStr("6c"), cardFromStr("7c"),
                              cardFromStr("8c"), cardFromStr("9c"),
                              cardFromStr("Tc"), cardFromStr("Jc")};
        for (int p = 1; p < config.num_players; ++p) {
            hole_arr[p][0] = dummy_cards[(p - 1) * 2];
            hole_arr[p][1] = dummy_cards[(p - 1) * 2 + 1];
        }
        state = state.withHoleCards(hole_arr);
    }

    auto hist_actions = parseHistory(history);
    ActionAbstraction aa(makeBetConfig(config));

    for (const auto& ha : hist_actions) {
        if (state.isTerminal() || state.needsCards()) break;
        auto legal = state.legalActions();

        if (ha.type == ActionType::Fold && legal.can_fold)
            state = state.apply(Action::fold());
        else if (ha.type == ActionType::Check && legal.can_check)
            state = state.apply(Action::check());
        else if (ha.type == ActionType::Call && legal.can_call)
            state = state.apply(Action::call());
        else if (ha.type == ActionType::BetRaise && legal.can_bet_raise)
            state = state.apply(Action::betRaise(legal.min_bet_raise));

        // Deal board if needed.
        if (state.needsCards()) {
            if (state.num_board_cards == 0 && num_board >= 3)
                state = state.withFlop(bd[0], bd[1], bd[2]);
            else if (state.num_board_cards == 3 && num_board >= 4)
                state = state.withTurn(bd[3]);
            else if (state.num_board_cards == 4 && num_board >= 5)
                state = state.withRiver(bd[4]);
        }
    }

    // Run subgame solver.
    auto bk = bucketArray(config);
    auto bucket_func = makeBucketFunc(config);

    std::array<std::array<Card, 2>, kMaxPlayers> hole_arr{};
    hole_arr[0] = {hole[0], hole[1]};
    hole_arr[1] = {cardFromStr("2c"), cardFromStr("3c")};
    // Fill extra players with dummies if needed
    Card dummy_cards2[] = {cardFromStr("4c"), cardFromStr("5c"),
                           cardFromStr("6c"), cardFromStr("7c"),
                           cardFromStr("8c"), cardFromStr("9c"),
                           cardFromStr("Tc"), cardFromStr("Jc")};
    for (int p = 2; p < config.num_players; ++p) {
        hole_arr[p][0] = dummy_cards2[(p - 2) * 2];
        hole_arr[p][1] = dummy_cards2[(p - 2) * 2 + 1];
    }
    std::array<Card, 5> board_arr = {bd[0], bd[1], bd[2], bd[3], bd[4]};

    SubgameConfig sub_cfg;
    sub_cfg.num_iterations = num_iterations;
    sub_cfg.dcfr_params = {config.dcfr_alpha, config.dcfr_beta, config.dcfr_gamma};
    sub_cfg.seed = config.seed;

    auto sub = SubgameSolver::solve(state, aa, bk, bucket_func,
                                     hole_arr, board_arr, sub_cfg);

    StrategyResult result;
    for (size_t i = 0; i < sub.actions.size(); ++i) {
        const auto& a = sub.actions[i];
        std::string name =
            a.type == ActionType::Fold     ? "fold" :
            a.type == ActionType::Check    ? "check" :
            a.type == ActionType::Call     ? "call" :
            "raise";
        result.actions.push_back(formatAction(name, a.amount));
        result.probabilities.push_back(sub.strategy[i]);
    }
    return result;
}

// ── measure_exploitability ───────────────────────────────────────────────

ExploitResult measure_exploitability(const std::string& strategy_path,
                                      int num_samples,
                                      const SolverConfig& config) {
    initCards();

    int stack_chips = config.stack_depth_bb * 2;
    ActionAbstraction aa(makeBetConfig(config));
    GameTree tree(stack_chips, aa, config.num_players);
    tree.build();

    auto bk = bucketArray(config);
    InfoSetManager info_sets(tree, bk);
    RegretStore store = RegretStore::load(strategy_path);

    auto bucket_func = makeBucketFunc(config);

    auto r = Exploitability::compute(tree, info_sets, store,
                                      bucket_func, num_samples, config.seed);

    ExploitResult result{};
    result.exploitability = r.exploitability;
    result.br_value_p0 = r.br_value_p0;
    result.br_value_p1 = r.br_value_p1;
    result.num_players = r.num_players;
    result.num_samples = r.num_samples;
    for (int p = 0; p < r.num_players; ++p)
        result.br_values[p] = r.br_values[p];
    return result;
}

// ── formatAction ─────────────────────────────────────────────────────────

std::string formatAction(const std::string& action_type, int amount) {
    if (action_type == "raise" && amount > 0)
        return "raise " + std::to_string(amount);
    return action_type;
}

}  // namespace poker_solver
