#include "tree/game_tree.h"

#include <cassert>

// ── Constructor ─────────────────────────────────────────────────────────
GameTree::GameTree(int stack_size, const ActionAbstraction& aa,
                   int num_players)
    : stack_size_(stack_size), num_players_(num_players), aa_(aa) {}

// ── Deal dummy cards to advance past chance nodes ───────────────────────
// Legal actions depend only on pot/stack/bet state, not card values.
// We use non-conflicting dummy cards: P0={0,1}, P1={2,3},
// flop={4,5,6}, turn={7}, river={8}.

GameState GameTree::dealDummyCards(GameState state) {
    // Deal hole cards if this is a fresh hand (preflop, no cards yet)
    if (state.street == Street::Preflop && state.hole[0][0] == kNoCard) {
        // Deal num_players * 2 hole cards using non-conflicting dummy values.
        // P0={0,1}, P1={2,3}, P2={4,5}, ...
        std::array<std::array<Card, 2>, kMaxPlayers> hole{};
        Card next = 0;
        for (int p = 0; p < state.num_players; ++p) {
            hole[p][0] = next++;
            hole[p][1] = next++;
        }
        state = state.withHoleCards(hole);
    }
    // Board cards start after all hole cards.
    // For 6 players: hole cards use 0-11, board starts at 12.
    Card board_start = static_cast<Card>(state.num_players * 2);
    while (state.needsCards()) {
        switch (state.street) {
            case Street::Flop:
                state = state.withFlop(board_start, board_start + 1,
                                       board_start + 2);
                break;
            case Street::Turn:
                state = state.withTurn(board_start + 3);
                break;
            case Street::River:
                state = state.withRiver(board_start + 4);
                break;
            default:
                break;
        }
    }
    return state;
}

// ── Build ───────────────────────────────────────────────────────────────
void GameTree::build() {
    nodes_.clear();
    children_.clear();
    actions_.clear();
    info_set_counter_ = 0;
    num_action_nodes_ = 0;
    num_terminal_nodes_ = 0;

    auto root_state = GameState::newHand(stack_size_, 0, num_players_);
    root_state = dealDummyCards(root_state);
    buildNode(root_state);
}

// ── Recursive tree builder ──────────────────────────────────────────────
uint32_t GameTree::buildNode(GameState state) {
    // Advance past any chance nodes (card deals).
    state = dealDummyCards(state);

    if (state.isTerminal()) {
        uint32_t idx = static_cast<uint32_t>(nodes_.size());
        TreeNode n{};
        n.type         = NodeType::Terminal;
        n.player       = 0;
        n.street       = static_cast<uint8_t>(state.street);
        n.num_actions  = 0;
        n.children_start = 0;
        n.info_set_idx = 0;
        n.pot          = state.potSize();
        n.fold_player  = static_cast<int8_t>(state.foldWinner());
        nodes_.push_back(n);
        num_terminal_nodes_++;
        return idx;
    }

    // Get abstract actions for this state.
    auto abs_actions = aa_.getActions(state);
    int num_acts = static_cast<int>(abs_actions.size());
    assert(num_acts > 0);

    // Create node placeholder (index stable through recursion).
    uint32_t idx = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back({});

    // Allocate children and actions slots (same offset for parallel indexing).
    uint32_t offset = static_cast<uint32_t>(children_.size());
    children_.resize(offset + num_acts, 0);
    actions_.resize(offset + num_acts);
    for (int i = 0; i < num_acts; ++i)
        actions_[offset + i] = abs_actions[i];

    // Recurse for each action.
    for (int i = 0; i < num_acts; ++i) {
        GameState child_state = state.apply(abs_actions[i]);
        children_[offset + i] = buildNode(child_state);
    }

    // Fill in the node.
    TreeNode& n    = nodes_[idx];
    n.type         = NodeType::Action;
    n.player       = static_cast<uint8_t>(state.actor);
    n.street       = static_cast<uint8_t>(state.street);
    n.num_actions  = static_cast<uint8_t>(num_acts);
    n.children_start = offset;
    n.info_set_idx = info_set_counter_++;
    n.pot          = 0;
    n.fold_player  = -1;

    num_action_nodes_++;
    return idx;
}
