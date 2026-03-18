#include "tree/game_tree.h"

#include <gtest/gtest.h>
#include <set>

// ── Basic tree builds without crashing ─────────────────────────────────

TEST(GameTree, BuildsSuccessfully) {
    ActionAbstraction aa;
    GameTree tree(200, aa);  // 100bb = 200 chips
    tree.build();

    EXPECT_GT(tree.numNodes(), 0u);
    EXPECT_GT(tree.numActionNodes(), 0u);
    EXPECT_GT(tree.numTerminalNodes(), 0u);
    EXPECT_GT(tree.numInfoSets(), 0u);
}

// ── Root is an action node for preflop ─────────────────────────────────

TEST(GameTree, RootIsPreflop) {
    ActionAbstraction aa;
    GameTree tree(200, aa);
    tree.build();

    const auto& r = tree.root();
    EXPECT_EQ(r.type, NodeType::Action);
    EXPECT_EQ(r.street, 0);  // preflop
    EXPECT_GT(r.num_actions, 0);
}

// ── Terminal nodes have valid pot and fold info ─────────────────────────

TEST(GameTree, TerminalNodesValid) {
    ActionAbstraction aa;
    GameTree tree(200, aa);
    tree.build();

    int terminal_count = 0;
    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type == NodeType::Terminal) {
            terminal_count++;
            EXPECT_GT(n.pot, 0) << "Terminal pot should be positive";
            EXPECT_TRUE(n.fold_player == -1 || n.fold_player == 0 || n.fold_player == 1)
                << "fold_player should be -1, 0, or 1";
        }
    }
    EXPECT_GT(terminal_count, 0);
}

// ── Info set indices are unique and contiguous ─────────────────────────

TEST(GameTree, InfoSetIndicesUnique) {
    ActionAbstraction aa;
    GameTree tree(200, aa);
    tree.build();

    std::set<uint32_t> seen;
    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type == NodeType::Action) {
            EXPECT_TRUE(seen.insert(n.info_set_idx).second)
                << "Duplicate info_set_idx: " << n.info_set_idx;
        }
    }
    EXPECT_EQ(static_cast<uint32_t>(seen.size()), tree.numInfoSets());

    // Should be contiguous: 0, 1, 2, ..., N-1
    if (!seen.empty()) {
        EXPECT_EQ(*seen.begin(), 0u);
        EXPECT_EQ(*seen.rbegin(), tree.numInfoSets() - 1);
    }
}

// ── Children accessible and valid ──────────────────────────────────────

TEST(GameTree, ChildrenAccessible) {
    ActionAbstraction aa;
    GameTree tree(200, aa);
    tree.build();

    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type == NodeType::Action) {
            for (int a = 0; a < n.num_actions; ++a) {
                uint32_t child_idx = tree.child(i, a);
                EXPECT_LT(child_idx, tree.numNodes())
                    << "Child index out of range at node " << i << " action " << a;

                // Action should be valid
                const auto& act = tree.action(i, a);
                EXPECT_TRUE(act.type == ActionType::Fold ||
                            act.type == ActionType::Check ||
                            act.type == ActionType::Call ||
                            act.type == ActionType::BetRaise);
            }
        }
    }
}

// ── Fold always leads to terminal ──────────────────────────────────────

TEST(GameTree, FoldLeadsToTerminal) {
    ActionAbstraction aa;
    GameTree tree(200, aa);
    tree.build();

    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type != NodeType::Action) continue;

        for (int a = 0; a < n.num_actions; ++a) {
            if (tree.action(i, a).type == ActionType::Fold) {
                uint32_t child_idx = tree.child(i, a);
                EXPECT_EQ(tree.node(child_idx).type, NodeType::Terminal)
                    << "Fold at node " << i << " should lead to terminal";
                EXPECT_NE(tree.node(child_idx).fold_player, -1)
                    << "Fold terminal should have a fold_player";
            }
        }
    }
}

// ── Tree has all 4 streets ─────────────────────────────────────────────

TEST(GameTree, AllStreetsPresent) {
    ActionAbstraction aa;
    GameTree tree(200, aa);
    tree.build();

    bool has_street[4] = {};
    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type == NodeType::Action && n.street < 4)
            has_street[n.street] = true;
    }
    EXPECT_TRUE(has_street[0]) << "Missing preflop action nodes";
    EXPECT_TRUE(has_street[1]) << "Missing flop action nodes";
    EXPECT_TRUE(has_street[2]) << "Missing turn action nodes";
    EXPECT_TRUE(has_street[3]) << "Missing river action nodes";
}

// ── Small stack tree is manageable ─────────────────────────────────────

TEST(GameTree, SmallStackTreeSize) {
    // 10bb = 20 chips. Very short stacks → small tree.
    ActionAbstraction aa;
    GameTree tree(20, aa);
    tree.build();

    // Should be relatively small
    EXPECT_GT(tree.numNodes(), 10u);
    EXPECT_LT(tree.numNodes(), 100000u)
        << "10bb tree should be small";
}

// ── Deeper stack produces larger tree ──────────────────────────────────

TEST(GameTree, DeeperStackLargerTree) {
    ActionAbstraction aa;

    GameTree small_tree(20, aa);   // 10bb
    small_tree.build();

    GameTree large_tree(200, aa);  // 100bb
    large_tree.build();

    EXPECT_GT(large_tree.numNodes(), small_tree.numNodes())
        << "100bb tree should be larger than 10bb tree";
}

// ── More bet sizes → larger tree ───────────────────────────────────────

TEST(GameTree, MoreBetSizesLargerTree) {
    BetSizeConfig few;
    few.preflop = {1.0};
    few.flop = {1.0};
    few.turn = {1.0};
    few.river = {1.0};

    BetSizeConfig many;
    many.preflop = {0.33, 0.67, 1.0, 1.5};
    many.flop = many.preflop;
    many.turn = many.preflop;
    many.river = many.preflop;

    GameTree few_tree(200, ActionAbstraction(few));
    few_tree.build();

    GameTree many_tree(200, ActionAbstraction(many));
    many_tree.build();

    EXPECT_GT(many_tree.numNodes(), few_tree.numNodes())
        << "More bet sizes should produce a larger tree";
}

// ── Both players appear as actors ──────────────────────────────────────

TEST(GameTree, BothPlayersAct) {
    ActionAbstraction aa;
    GameTree tree(200, aa);
    tree.build();

    bool player_acts[2] = {};
    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type == NodeType::Action)
            player_acts[n.player] = true;
    }
    EXPECT_TRUE(player_acts[0]) << "Player 0 should have action nodes";
    EXPECT_TRUE(player_acts[1]) << "Player 1 should have action nodes";
}

// ── Showdown terminals exist ───────────────────────────────────────────

TEST(GameTree, ShowdownTerminalsExist) {
    ActionAbstraction aa;
    GameTree tree(200, aa);
    tree.build();

    bool has_showdown = false;
    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type == NodeType::Terminal && n.fold_player == -1)
            has_showdown = true;
    }
    EXPECT_TRUE(has_showdown) << "Should have showdown terminal nodes";
}
