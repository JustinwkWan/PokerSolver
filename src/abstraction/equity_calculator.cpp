#include "abstraction/equity_calculator.h"

#include <algorithm>
#include <cassert>

EquityCalculator::EquityCalculator() : eval_() {}

// ── River equity (exact) ─────────────────────────────────────────────────
double EquityCalculator::riverEquityExact(
    const std::array<Card, 2>& hole,
    const std::array<Card, 5>& board) const {

    // Build dead-card mask: hero hole (2) + board (5) = 7 cards.
    CardMask dead = cardBit(hole[0]) | cardBit(hole[1]);
    for (Card c : board) dead |= cardBit(c);
    assert(__builtin_popcountll(dead) == 7);

    // Evaluate hero once (same hand vs all opponents).
    Card hero7[7] = {hole[0], hole[1],
                     board[0], board[1], board[2], board[3], board[4]};
    uint16_t hero_rank = eval_.evaluate(hero7, 7);

    // Enumerate all C(45,2) = 990 opponent hands.
    double wins = 0;
    int count = 0;

    for (int o1 = 0; o1 < kNumCards; ++o1) {
        if (dead & cardBit(o1)) continue;
        for (int o2 = o1 + 1; o2 < kNumCards; ++o2) {
            if (dead & cardBit(o2)) continue;

            Card opp7[7] = {Card(o1), Card(o2),
                            board[0], board[1], board[2], board[3], board[4]};
            uint16_t opp_rank = eval_.evaluate(opp7, 7);

            if (hero_rank > opp_rank)      wins += 1.0;
            else if (hero_rank == opp_rank) wins += 0.5;
            // else: loss, add nothing

            ++count;
        }
    }

    assert(count == 990);  // C(45,2)
    return wins / count;
}

// ── Batch river equity ───────────────────────────────────────────────────
std::vector<EquityCalculator::HoleEquity>
EquityCalculator::allRiverEquities(const std::array<Card, 5>& board) const {

    CardMask board_mask = 0;
    for (Card c : board) board_mask |= cardBit(c);
    assert(__builtin_popcountll(board_mask) == 5);

    std::vector<HoleEquity> results;
    results.reserve(1081);  // C(47,2)

    for (int c1 = 0; c1 < kNumCards; ++c1) {
        if (board_mask & cardBit(c1)) continue;
        for (int c2 = c1 + 1; c2 < kNumCards; ++c2) {
            if (board_mask & cardBit(c2)) continue;

            std::array<Card, 2> h = {Card(c1), Card(c2)};
            double eq = riverEquityExact(h, board);
            results.push_back({h, eq});
        }
    }

    assert(static_cast<int>(results.size()) == 1081);  // C(47,2)
    return results;
}

// ── Monte Carlo equity (flop/turn) ───────────────────────────────────────
double EquityCalculator::equityMonteCarlo(
    const std::array<Card, 2>& hole,
    const Card* board_cards, int num_board,
    int num_samples,
    std::mt19937_64& rng) const {

    assert(num_board >= 0 && num_board <= 4);

    // Build dead mask and live card array.
    CardMask dead = cardBit(hole[0]) | cardBit(hole[1]);
    for (int i = 0; i < num_board; ++i) dead |= cardBit(board_cards[i]);

    int num_dead = __builtin_popcountll(dead);
    assert(num_dead == 2 + num_board);

    int num_live = kNumCards - num_dead;
    std::vector<Card> live;
    live.reserve(num_live);
    for (int c = 0; c < kNumCards; ++c) {
        if (!(dead & cardBit(c))) live.push_back(Card(c));
    }
    assert(static_cast<int>(live.size()) == num_live);

    // Each sample needs: 2 opponent cards + (5 - num_board) board completions.
    int cards_needed = 2 + (5 - num_board);

    double wins = 0;

    for (int s = 0; s < num_samples; ++s) {
        // Fisher-Yates partial shuffle: select cards_needed random cards.
        for (int i = 0; i < cards_needed; ++i) {
            std::uniform_int_distribution<int> dist(i, num_live - 1);
            int j = dist(rng);
            std::swap(live[i], live[j]);
        }

        // First 2 are opponent's hole cards.
        Card opp1 = live[0], opp2 = live[1];

        // Build full 5-card board.
        Card full_board[5];
        for (int i = 0; i < num_board; ++i) full_board[i] = board_cards[i];
        for (int i = num_board; i < 5; ++i) full_board[i] = live[2 + (i - num_board)];

        // Evaluate both 7-card hands.
        Card hero7[7] = {hole[0], hole[1],
                         full_board[0], full_board[1], full_board[2],
                         full_board[3], full_board[4]};
        Card opp7[7]  = {opp1, opp2,
                         full_board[0], full_board[1], full_board[2],
                         full_board[3], full_board[4]};

        uint16_t hero_rank = eval_.evaluate(hero7, 7);
        uint16_t opp_rank  = eval_.evaluate(opp7, 7);

        if (hero_rank > opp_rank)      wins += 1.0;
        else if (hero_rank == opp_rank) wins += 0.5;
    }

    return wins / num_samples;
}
