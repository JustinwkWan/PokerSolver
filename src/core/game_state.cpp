#include "core/game_state.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

// ── Helpers ────────────────────────────────────────────────────────────────

static int currentBet(const GameState& s) {
    return std::max(s.street_bet[0], s.street_bet[1]);
}

// ── Factory ────────────────────────────────────────────────────────────────

GameState GameState::newHand(int stack_size_chips, int button_seat) {
    assert(stack_size_chips >= kBB * 2 && "stack must cover at least 2 BB");
    assert(button_seat == 0 || button_seat == 1);

    GameState s{};
    s.stack_size    = stack_size_chips;
    s.button        = button_seat;
    s.street        = Street::Preflop;
    s.num_board_cards = 0;
    s.pot           = 0;
    s.last_raise_size = kBB;  // initial min-raise = 1 BB

    // Fill board with kNoCard
    s.board.fill(kNoCard);

    int sb = button_seat;
    int bb = 1 - button_seat;

    // Post blinds (capped by stack)
    int sb_post = std::min(kSB, stack_size_chips);
    int bb_post = std::min(kBB, stack_size_chips);

    s.stacks[sb] = stack_size_chips - sb_post;
    s.stacks[bb] = stack_size_chips - bb_post;

    s.street_bet[sb] = sb_post;
    s.street_bet[bb] = bb_post;

    s.all_in[sb] = (s.stacks[sb] == 0);
    s.all_in[bb] = (s.stacks[bb] == 0);

    s.folded[0] = s.folded[1] = false;
    s.has_decided[0] = s.has_decided[1] = false;
    s.hand_over = false;

    // Preflop: button (SB) acts first in heads-up
    s.actor = sb;

    return s;
}

// ── Card dealing ───────────────────────────────────────────────────────────

GameState GameState::withHoleCards(Card p0c1, Card p0c2, Card p1c1, Card p1c2) const {
    GameState s = *this;
    s.hole[0] = {p0c1, p0c2};
    s.hole[1] = {p1c1, p1c2};
    return s;
}

GameState GameState::withFlop(Card c1, Card c2, Card c3) const {
    assert(needsCards() && street == Street::Flop);
    GameState s = *this;
    s.board[0] = c1; s.board[1] = c2; s.board[2] = c3;
    s.num_board_cards = 3;
    return s;
}

GameState GameState::withTurn(Card c) const {
    assert(needsCards() && street == Street::Turn);
    GameState s = *this;
    s.board[3] = c;
    s.num_board_cards = 4;
    return s;
}

GameState GameState::withRiver(Card c) const {
    assert(needsCards() && street == Street::River);
    GameState s = *this;
    s.board[4] = c;
    s.num_board_cards = 5;
    return s;
}

// ── Query ──────────────────────────────────────────────────────────────────

bool GameState::isTerminal() const {
    return hand_over || folded[0] || folded[1];
}

// Expected board cards once each street's betting opens
static int expectedBoardCards(Street s) {
    switch (s) {
        case Street::Preflop: return 0;
        case Street::Flop:    return 3;
        case Street::Turn:    return 4;
        case Street::River:   return 5;
    }
    return 0;
}

bool GameState::needsCards() const {
    if (isTerminal()) return false;
    return num_board_cards < expectedBoardCards(street);
}

bool GameState::isBettingClosed() const {
    if (folded[0] || folded[1]) return true;

    // If both all-in, no more betting
    if (all_in[0] && all_in[1]) return true;

    // Bets must be matched
    if (street_bet[0] != street_bet[1]) return false;

    // Both players must have had at least one decision this street
    return has_decided[0] && has_decided[1];
}

LegalActionInfo GameState::legalActions() const {
    assert(!isTerminal() && !needsCards());

    LegalActionInfo info{};
    int p   = actor;
    int opp = 1 - p;

    int my_bet  = street_bet[p];
    int opp_bet = street_bet[opp];
    int cur_bet = std::max(my_bet, opp_bet);
    int to_call = cur_bet - my_bet;

    info.can_fold  = (to_call > 0);   // no point folding if can check
    info.can_check = (to_call == 0);
    info.can_call  = (to_call > 0) && !all_in[p];
    info.call_amount = to_call;

    // Can we still raise?
    bool opp_all_in   = all_in[opp];
    bool i_am_all_in  = all_in[p];
    int  my_remaining = stacks[p];

    if (!i_am_all_in && !opp_all_in && my_remaining > to_call) {
        // Minimum total committed = cur_bet + last_raise_size
        int min_total = cur_bet + last_raise_size;
        // All-in total committed
        int allin_total = my_bet + my_remaining;

        info.can_bet_raise  = (min_total <= allin_total);
        info.min_bet_raise  = std::min(min_total, allin_total);
        info.max_bet_raise  = allin_total;
    } else if (!i_am_all_in && opp_all_in && my_remaining > to_call) {
        // Can call; cannot raise beyond opp's all-in
        // Actually can't raise more than opp's commitment
        info.can_bet_raise  = false;
    } else {
        info.can_bet_raise  = false;
    }

    return info;
}

// ── Apply ──────────────────────────────────────────────────────────────────

GameState GameState::apply(const Action& a) const {
    assert(!isTerminal() && !needsCards());

    GameState s = *this;
    int p   = s.actor;
    int opp = 1 - p;

    s.has_decided[p] = true;

    switch (a.type) {
        case ActionType::Fold:
            s.folded[p] = true;
            // pot stays; winner collects in foldWinner()
            return s;

        case ActionType::Check:
            assert(s.street_bet[p] == s.street_bet[opp]);
            if (s.isBettingClosed()) return s.advanceStreet();
            s.actor = opp;
            return s;

        case ActionType::Call: {
            int to_call = std::max(s.street_bet[0], s.street_bet[1]) - s.street_bet[p];
            int actual  = std::min(to_call, s.stacks[p]);  // cap at stack (side pot edge case)
            s.stacks[p]    -= actual;
            s.street_bet[p] += actual;
            s.all_in[p]     = (s.stacks[p] == 0);
            if (s.isBettingClosed()) return s.advanceStreet();
            s.actor = opp;
            return s;
        }

        case ActionType::BetRaise: {
            // a.amount = total committed by this player this street after the action
            int new_total = a.amount;
            assert(new_total > s.street_bet[p]);
            int added = new_total - s.street_bet[p];
            assert(added <= s.stacks[p]);

            int raise_by = new_total - std::max(s.street_bet[p], s.street_bet[opp]);
            s.last_raise_size = std::max(raise_by, kBB);

            s.stacks[p]    -= added;
            s.street_bet[p] = new_total;
            s.all_in[p]     = (s.stacks[p] == 0);

            // Opponent must respond
            s.has_decided[opp] = false;
            s.actor = opp;
            return s;
        }
    }
    __builtin_unreachable();
}

// ── Street transition ──────────────────────────────────────────────────────

GameState GameState::advanceStreet() const {
    GameState s = *this;

    // Collect street bets into pot
    s.pot += s.street_bet[0] + s.street_bet[1];
    s.street_bet[0] = s.street_bet[1] = 0;

    s.has_decided[0] = s.has_decided[1] = false;
    s.last_raise_size = kBB;

    if (s.street == Street::River) {
        s.hand_over = true;  // showdown — isTerminal() returns true
        return s;
    }

    s.street = static_cast<Street>(static_cast<int>(s.street) + 1);

    // Postflop: OOP (non-button) acts first
    int oop = 1 - s.button;
    int ip  = s.button;

    // Skip all-in players; if both all-in, needsCards() will be true
    if (s.all_in[oop] && !s.all_in[ip]) {
        s.actor = ip;
    } else {
        s.actor = oop;
    }

    // needsCards() will return true now (cards haven't been dealt yet)
    return s;
}

// ── Terminal payoffs ───────────────────────────────────────────────────────

int GameState::foldWinner() const {
    if (folded[0]) return 1;
    if (folded[1]) return 0;
    return -1;  // no fold
}
