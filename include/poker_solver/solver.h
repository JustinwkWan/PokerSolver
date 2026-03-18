#pragma once

// ── Public API for the GTO Poker Solver ──────────────────────────────────
//
// This header provides a clean interface for embedding the solver in other
// applications. All functions are in the poker_solver namespace.

#include <array>
#include <string>
#include <vector>

namespace poker_solver {

// ── Configuration ────────────────────────────────────────────────────────

struct SolverConfig {
    int stack_depth_bb = 100;
    int num_iterations = 1000000;
    std::string output_path = "data/strategy";

    // Bucket counts per street.
    std::array<int, 4> buckets = {169, 500, 1000, 2000};

    // Bet sizes as pot fractions per street.
    std::vector<double> preflop_sizes = {0.5, 1.0};
    std::vector<double> flop_sizes    = {0.5, 1.0};
    std::vector<double> turn_sizes    = {0.5, 1.0};
    std::vector<double> river_sizes   = {0.5, 1.0};

    // DCFR parameters.
    float dcfr_alpha = 1.5f;
    float dcfr_beta  = 0.0f;
    float dcfr_gamma = 2.0f;

    // Checkpointing.
    int checkpoint_every = 100000;  // iterations between checkpoints

    // Random seed.
    uint64_t seed = 42;
};

// ── Strategy query result ────────────────────────────────────────────────

struct StrategyResult {
    std::vector<std::string> actions;       // human-readable action names
    std::vector<float>       probabilities; // probability for each action
};

// ── Solve ────────────────────────────────────────────────────────────────

// Run the solver from scratch. Writes strategy files to config.output_path.
void solve(const SolverConfig& config);

// Resume from a checkpoint directory.
void solve_resume(const SolverConfig& config, const std::string& checkpoint_path);

// ── Query ────────────────────────────────────────────────────────────────

// Query the average strategy for a specific spot.
//   hole_cards: e.g. "AhKs"
//   board:      e.g. "Td9c2h" (3-5 cards) or "" for preflop
//   history:    action sequence e.g. "rc" (raise, call), "rcbf" etc.
//              f=fold, k=check, c=call, b=bet/raise
StrategyResult query(const std::string& strategy_path,
                     const std::string& hole_cards,
                     const std::string& board,
                     const std::string& history,
                     const SolverConfig& config);

// ── Subgame solving ──────────────────────────────────────────────────────

// Re-solve a subgame for a specific spot with (optionally) finer granularity.
StrategyResult solve_subgame(const std::string& hole_cards,
                              const std::string& board,
                              const std::string& history,
                              int num_iterations,
                              const SolverConfig& config);

// ── Exploitability ───────────────────────────────────────────────────────

struct ExploitResult {
    double exploitability;  // average chips exploitable per hand
    double br_value_p0;
    double br_value_p1;
    int    num_samples;
};

// Estimate exploitability of a solved strategy.
ExploitResult measure_exploitability(const std::string& strategy_path,
                                      int num_samples,
                                      const SolverConfig& config);

// ── Helpers ──────────────────────────────────────────────────────────────

// Format an action for display: "fold", "check", "call", "raise 150"
std::string formatAction(const std::string& action_type, int amount = 0);

}  // namespace poker_solver
