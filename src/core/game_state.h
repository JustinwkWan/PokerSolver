#pragma once

#include "core/cards.h"
#include <array>
#include <cstdint>
#include <vector>

// ── Chip convention ────────────────────────────────────────────────────────
// 1 unit = 0.5 BB   →   SB = 1, BB = 2, 100bb stack = 200 chips.
// This avoids fractional chips while keeping blind values integer.

constexpr int kSB = 1;
constexpr int kBB = 2;

// ── Enums ──────────────────────────────────────────────────────────────────
enum class Street : uint8_t { Preflop = 0, Flop = 1, Turn = 2, River = 3 };

enum class ActionType : uint8_t { Fold, Check, Call, BetRaise };

struct Action {
    ActionType type;
    int        amount;  // total committed this street after BetRaise; 0 otherwise

    static Action fold()          { return {ActionType::Fold,     0}; }
    static Action check()         { return {ActionType::Check,    0}; }
    static Action call()          { return {ActionType::Call,     0}; }
    static Action betRaise(int a) { return {ActionType::BetRaise, a}; }

    bool operator==(const Action& o) const {
        return type == o.type && amount == o.amount;
    }
};

// ── LegalActionInfo ────────────────────────────────────────────────────────
// Caller uses this to construct or validate Actions.
struct LegalActionInfo {
    bool can_fold;
    bool can_check;
    bool can_call;
    int  call_amount;    // chips needed to call (0 when can_call is false)
    bool can_bet_raise;
    int  min_bet_raise;  // minimum total-committed value for BetRaise
    int  max_bet_raise;  // all-in value (== min_bet_raise if already all-in)
};

// ── GameState ──────────────────────────────────────────────────────────────
// Immutable-style: apply() returns a new state; this one is not modified.
//
// Street transitions happen internally when betting closes.
// The NEW state after a street closes will have needsCards() == true,
// signalling the caller to call deal*() before continuing.

struct GameState {
    // ── Configuration (set at hand start, never changes) ──────────────────
    int stack_size;   // chips per player at start of hand

    // ── Cards ─────────────────────────────────────────────────────────────
    std::array<std::array<Card, 2>, 2> hole;  // hole[player][0..1]
    std::array<Card, 5>                board; // kNoCard until dealt
    int                                num_board_cards;

    // ── Pot / stacks ──────────────────────────────────────────────────────
    int pot;            // chips collected into pot from PREVIOUS streets
    int stacks[2];      // current stacks (chips NOT yet in pot or bet)
    int street_bet[2];  // chips each player has committed THIS street

    // ── Betting state ─────────────────────────────────────────────────────
    Street street;
    int    actor;            // whose turn (0 or 1)
    int    button;           // dealer / small blind (0 or 1)
    bool   folded[2];
    bool   all_in[2];
    bool   has_decided[2];   // has made ≥1 real decision this street
    int    last_raise_size;  // size of the last raise (for min-raise rule)
    bool   hand_over;        // true after river showdown (fold is detected via folded[])

    // ── Factory ───────────────────────────────────────────────────────────
    // Creates state with blinds posted, ready for preflop hole card deal.
    static GameState newHand(int stack_size_chips, int button_seat);

    // ── Card dealing ──────────────────────────────────────────────────────
    // Call after needsCards() returns true.  Returns a copy with cards set.
    GameState withHoleCards(Card p0c1, Card p0c2, Card p1c1, Card p1c2) const;
    GameState withFlop(Card c1, Card c2, Card c3) const;
    GameState withTurn(Card c) const;
    GameState withRiver(Card c) const;

    // ── Query ─────────────────────────────────────────────────────────────
    bool isTerminal()  const;  // hand is over (fold or all streets complete)
    bool needsCards()  const;  // it's a chance node: cards must be dealt

    LegalActionInfo legalActions() const;

    // Total chips in the middle right now
    int potSize() const { return pot + street_bet[0] + street_bet[1]; }

    // ── State transition ──────────────────────────────────────────────────
    // Returns next state after applying action.  Asserts action is legal.
    GameState apply(const Action& a) const;

    // ── Terminal payoffs ──────────────────────────────────────────────────
    // If someone folded, returns the winner index (0 or 1).
    // Returns -1 if the hand ends in a showdown (caller evaluates hands).
    int foldWinner() const;

private:
    // Called from apply() after a call/check that closes betting.
    GameState advanceStreet() const;
    bool      isBettingClosed() const;
};
