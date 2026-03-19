#include "core/game_state.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

// ── Helpers ────────────────────────────────────────────────────────────────

static int currentBet(const GameState& s) {
    int mx = 0;
    for (int i = 0; i < s.num_players; ++i)
        mx = std::max(mx, s.street_bet[i]);
    return mx;
}

// ── N-player helpers ───────────────────────────────────────────────────────

int GameState::numNonFolded() const {
    int count = 0;
    for (int i = 0; i < num_players; ++i)
        if (!folded[i]) ++count;
    return count;
}

int GameState::nextActivePlayer(int from) const {
    for (int i = 1; i <= num_players; ++i) {
        int p = (from + i) % num_players;
        if (!folded[p] && !all_in[p]) return p;
    }
    return -1;  // all folded or all-in
}

// ── Factory ────────────────────────────────────────────────────────────────

GameState GameState::newHand(int stack_size_chips, int button_seat,
                             int num_players_in) {
    assert(stack_size_chips >= kBB * 2 && "stack must cover at least 2 BB");
    assert(num_players_in >= 2 && num_players_in <= kMaxPlayers);
    assert(button_seat >= 0 && button_seat < num_players_in);

    GameState s{};
    std::memset(&s, 0, sizeof(s));
    s.stack_size    = stack_size_chips;
    s.num_players   = num_players_in;
    s.button        = button_seat;
    s.street        = Street::Preflop;
    s.num_board_cards = 0;
    s.pot           = 0;
    s.last_raise_size = kBB;  // initial min-raise = 1 BB
    s.hand_over     = false;

    // Fill board and hole cards with kNoCard
    s.board.fill(kNoCard);
    for (int i = 0; i < kMaxPlayers; ++i) {
        s.hole[i].fill(kNoCard);
    }

    // Initialize stacks
    for (int i = 0; i < num_players_in; ++i) {
        s.stacks[i] = stack_size_chips;
    }

    if (num_players_in == 2) {
        // HU: button = SB, acts first preflop
        int sb = button_seat;
        int bb = 1 - button_seat;

        int sb_post = std::min(kSB, stack_size_chips);
        int bb_post = std::min(kBB, stack_size_chips);

        s.stacks[sb] -= sb_post;
        s.stacks[bb] -= bb_post;

        s.street_bet[sb] = sb_post;
        s.street_bet[bb] = bb_post;

        s.all_in[sb] = (s.stacks[sb] == 0);
        s.all_in[bb] = (s.stacks[bb] == 0);

        s.actor = sb;  // SB acts first preflop in HU
    } else {
        // N>2: SB = (button+1)%N, BB = (button+2)%N, UTG = (button+3)%N
        int sb = (button_seat + 1) % num_players_in;
        int bb = (button_seat + 2) % num_players_in;

        int sb_post = std::min(kSB, stack_size_chips);
        int bb_post = std::min(kBB, stack_size_chips);

        s.stacks[sb] -= sb_post;
        s.stacks[bb] -= bb_post;

        s.street_bet[sb] = sb_post;
        s.street_bet[bb] = bb_post;

        s.all_in[sb] = (s.stacks[sb] == 0);
        s.all_in[bb] = (s.stacks[bb] == 0);

        // UTG acts first preflop (first player after BB)
        int utg = (button_seat + 3) % num_players_in;
        s.actor = utg;
    }

    return s;
}

// ── Card dealing ───────────────────────────────────────────────────────────

GameState GameState::withHoleCards(Card p0c1, Card p0c2, Card p1c1, Card p1c2) const {
    GameState s = *this;
    s.hole[0] = {p0c1, p0c2};
    s.hole[1] = {p1c1, p1c2};
    return s;
}

GameState GameState::withHoleCards(const std::array<std::array<Card, 2>, kMaxPlayers>& cards) const {
    GameState s = *this;
    for (int i = 0; i < num_players; ++i) {
        s.hole[i] = cards[i];
    }
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
    return hand_over || numNonFolded() <= 1;
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
    // If only one player remains, betting is done
    if (numNonFolded() <= 1) return true;

    // If all non-folded players are all-in, no more betting
    bool any_can_act = false;
    for (int i = 0; i < num_players; ++i) {
        if (!folded[i] && !all_in[i]) {
            any_can_act = true;
            break;
        }
    }
    if (!any_can_act) return true;

    // Bets must be matched among non-folded players
    int cur = currentBet(*this);
    for (int i = 0; i < num_players; ++i) {
        if (!folded[i] && !all_in[i] && street_bet[i] != cur)
            return false;
    }

    // All non-folded, non-all-in players must have had at least one decision
    for (int i = 0; i < num_players; ++i) {
        if (!folded[i] && !all_in[i] && !has_decided[i])
            return false;
    }

    return true;
}

LegalActionInfo GameState::legalActions() const {
    assert(!isTerminal() && !needsCards());

    LegalActionInfo info{};
    int p = actor;

    int cur_bet = currentBet(*this);
    int my_bet  = street_bet[p];
    int to_call = cur_bet - my_bet;

    info.can_fold  = (to_call > 0);   // no point folding if can check
    info.can_check = (to_call == 0);
    info.can_call  = (to_call > 0) && !all_in[p];
    info.call_amount = to_call;

    // Can we still raise? Need at least one non-folded non-all-in opponent
    bool has_active_opponent = false;
    for (int i = 0; i < num_players; ++i) {
        if (i != p && !folded[i] && !all_in[i]) {
            has_active_opponent = true;
            break;
        }
    }

    bool i_am_all_in  = all_in[p];
    int  my_remaining = stacks[p];

    if (!i_am_all_in && has_active_opponent && my_remaining > to_call) {
        // Minimum total committed = cur_bet + last_raise_size
        int min_total = cur_bet + last_raise_size;
        // All-in total committed
        int allin_total = my_bet + my_remaining;

        info.can_bet_raise  = (min_total <= allin_total);
        info.min_bet_raise  = std::min(min_total, allin_total);
        info.max_bet_raise  = allin_total;
    } else {
        info.can_bet_raise  = false;
    }

    return info;
}

// ── Apply ──────────────────────────────────────────────────────────────────

GameState GameState::apply(const Action& a) const {
    assert(!isTerminal() && !needsCards());

    GameState s = *this;
    int p = s.actor;

    s.has_decided[p] = true;

    switch (a.type) {
        case ActionType::Fold:
            s.folded[p] = true;
            // If only one player remains, hand is over
            if (s.numNonFolded() <= 1) return s;
            // Otherwise advance to next active player
            {
                int next = s.nextActivePlayer(p);
                if (next >= 0) s.actor = next;
                if (s.isBettingClosed()) return s.advanceStreet();
            }
            return s;

        case ActionType::Check:
            assert(s.street_bet[p] == currentBet(s));
            if (s.isBettingClosed()) return s.advanceStreet();
            {
                int next = s.nextActivePlayer(p);
                if (next >= 0) s.actor = next;
            }
            return s;

        case ActionType::Call: {
            int to_call = currentBet(s) - s.street_bet[p];
            int actual  = std::min(to_call, s.stacks[p]);
            s.stacks[p]    -= actual;
            s.street_bet[p] += actual;
            s.all_in[p]     = (s.stacks[p] == 0);
            if (s.isBettingClosed()) return s.advanceStreet();
            {
                int next = s.nextActivePlayer(p);
                if (next >= 0) s.actor = next;
            }
            return s;
        }

        case ActionType::BetRaise: {
            // a.amount = total committed by this player this street after the action
            int new_total = a.amount;
            assert(new_total > s.street_bet[p]);
            int added = new_total - s.street_bet[p];
            assert(added <= s.stacks[p]);

            int raise_by = new_total - currentBet(s);
            s.last_raise_size = std::max(raise_by, kBB);

            s.stacks[p]    -= added;
            s.street_bet[p] = new_total;
            s.all_in[p]     = (s.stacks[p] == 0);

            // All other non-folded non-all-in players must respond
            for (int i = 0; i < s.num_players; ++i) {
                if (i != p && !s.folded[i] && !s.all_in[i])
                    s.has_decided[i] = false;
            }

            {
                int next = s.nextActivePlayer(p);
                if (next >= 0) s.actor = next;
            }
            return s;
        }
    }
    __builtin_unreachable();
}

// ── Street transition ──────────────────────────────────────────────────────

GameState GameState::advanceStreet() const {
    GameState s = *this;

    // Collect street bets into pot
    for (int i = 0; i < s.num_players; ++i) {
        s.pot += s.street_bet[i];
        s.street_bet[i] = 0;
    }

    for (int i = 0; i < s.num_players; ++i) {
        s.has_decided[i] = false;
    }
    s.last_raise_size = kBB;

    if (s.street == Street::River) {
        s.hand_over = true;  // showdown — isTerminal() returns true
        return s;
    }

    s.street = static_cast<Street>(static_cast<int>(s.street) + 1);

    // Postflop: first active player after button
    int first = s.nextActivePlayer(s.button);
    if (first >= 0) {
        s.actor = first;
    }
    // If first < 0, all non-folded players are all-in; needsCards() will handle it

    // needsCards() will return true now (cards haven't been dealt yet)
    return s;
}

// ── Terminal payoffs ───────────────────────────────────────────────────────

int GameState::foldWinner() const {
    int winner = -1;
    int count = 0;
    for (int i = 0; i < num_players; ++i) {
        if (!folded[i]) {
            winner = i;
            ++count;
        }
    }
    if (count == 1) return winner;
    return -1;  // no fold (showdown or multiple players remain)
}
