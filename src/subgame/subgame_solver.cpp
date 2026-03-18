#include "subgame/subgame_solver.h"
#include "solver/exploitability.h"

#include <cassert>

// ── SubgameSolver ────────────────────────────────────────────────────────

SubgameResult SubgameSolver::solve(
        const GameState& state,
        const ActionAbstraction& aa,
        const std::array<int, 4>& buckets,
        const CFRSolver::BucketFunc& bucket_func,
        const std::array<std::array<Card, 2>, 2>& hole,
        const std::array<Card, 5>& board,
        const SubgameConfig& config) {

    // 1. Build a subtree from the given game state.
    //    The GameTree constructor takes a stack size and action abstraction.
    //    We create a tree rooted at this state's effective stack depth.
    //
    //    Note: GameTree builds from the start of a hand. For a true subgame,
    //    we use the remaining stacks as the "stack size" and the current pot
    //    is implicit in the terminal payoffs.
    //
    //    For now, build a full tree from the current street's effective depth.
    int effective_stack = state.stack_size;
    GameTree subtree(effective_stack, aa);
    subtree.build();

    // 2. Create info set manager and regret store for the subtree.
    InfoSetManager sub_mgr(subtree, buckets);
    RegretStore sub_store(sub_mgr.totalInfoSets(), sub_mgr.maxActions());

    // 3. Run DCFR on the subtree.
    CFRSolver solver(subtree, sub_mgr, sub_store, bucket_func,
                     config.dcfr_params, config.seed);
    solver.run(config.num_iterations);

    // 4. Extract the refined strategy at the root.
    const auto& root = subtree.root();
    assert(root.type == NodeType::Action);

    int player = root.player;
    int street  = root.street;
    int bucket = bucket_func(player, street, hole[player].data(),
                              board.data(),
                              state.num_board_cards);

    uint64_t info_id = sub_mgr.infoSetId(0, bucket);
    int num_actions = root.num_actions;

    SubgameResult result;
    result.strategy.resize(num_actions);
    sub_store.getAverageStrategy(player, info_id, num_actions,
                                  result.strategy.data());

    // Collect the actions.
    result.actions.reserve(num_actions);
    for (int a = 0; a < num_actions; ++a)
        result.actions.push_back(subtree.action(0, a));

    result.num_nodes = subtree.numNodes();
    result.num_info_sets = sub_mgr.totalInfoSets();
    result.num_iterations = config.num_iterations;

    return result;
}
