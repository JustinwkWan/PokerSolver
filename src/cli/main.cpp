// poker-solver CLI — unified entry point with subcommands.
//
// Usage:
//   poker-solver solve     [options]    Run DCFR solver
//   poker-solver query     [options]    Query a solved strategy
//   poker-solver exploit   [options]    Measure exploitability
//   poker-solver subgame   [options]    Real-time subgame solve
//   poker-solver help                   Show usage

#include "poker_solver/solver.h"
#include "server/http_server.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// ── Usage ────────────────────────────────────────────────────────────────

static void printUsage() {
    std::cout <<
        "Usage: poker-solver <command> [options]\n"
        "\n"
        "Commands:\n"
        "  solve      Run the DCFR solver\n"
        "  query      Query a solved strategy for a specific spot\n"
        "  exploit    Measure exploitability of a solved strategy\n"
        "  subgame    Re-solve a subgame with finer granularity\n"
        "  ui         Launch the web UI server\n"
        "  help       Show this help message\n"
        "\n"
        "Run 'poker-solver <command> --help' for command-specific options.\n";
}

static void printSolveUsage() {
    std::cout <<
        "Usage: poker-solver solve [options]\n"
        "\n"
        "Options:\n"
        "  --output DIR         Output directory for strategy files (default: data/strategy)\n"
        "  --iterations N       Number of DCFR iterations (default: 1000000)\n"
        "  --stack N            Stack depth in big blinds (default: 100)\n"
        "  --resume DIR         Resume from checkpoint directory\n"
        "  --checkpoint N       Iterations between checkpoints (default: 100000)\n"
        "  --alpha F            DCFR alpha parameter (default: 1.5)\n"
        "  --beta F             DCFR beta parameter (default: 0.0)\n"
        "  --gamma F            DCFR gamma parameter (default: 2.0)\n"
        "  --seed N             Random seed (default: 42)\n";
}

static void printQueryUsage() {
    std::cout <<
        "Usage: poker-solver query [options]\n"
        "\n"
        "Options:\n"
        "  --strategy DIR       Strategy directory (required)\n"
        "  --hand STR           Hole cards, e.g. \"AhKs\" (required)\n"
        "  --board STR          Board cards, e.g. \"Td9c2h\" (default: \"\" for preflop)\n"
        "  --history STR        Action history, e.g. \"rc\" (default: \"\")\n"
        "  --stack N            Stack depth in big blinds (default: 100)\n";
}

static void printExploitUsage() {
    std::cout <<
        "Usage: poker-solver exploit [options]\n"
        "\n"
        "Options:\n"
        "  --strategy DIR       Strategy directory (required)\n"
        "  --samples N          Number of Monte Carlo samples (default: 100000)\n"
        "  --stack N            Stack depth in big blinds (default: 100)\n"
        "  --seed N             Random seed (default: 42)\n";
}

static void printSubgameUsage() {
    std::cout <<
        "Usage: poker-solver subgame [options]\n"
        "\n"
        "Options:\n"
        "  --hand STR           Hole cards, e.g. \"AhKs\" (required)\n"
        "  --board STR          Board cards, e.g. \"Td9c2h7d\" (required)\n"
        "  --history STR        Action history, e.g. \"rcc\" (default: \"\")\n"
        "  --iterations N       DCFR iterations for subgame (default: 10000)\n"
        "  --stack N            Stack depth in big blinds (default: 100)\n"
        "  --seed N             Random seed (default: 42)\n";
}

// ── Argument helpers ─────────────────────────────────────────────────────

static std::string getArg(int argc, char* argv[], int& i) {
    if (i + 1 >= argc) {
        std::cerr << "Error: missing value for " << argv[i] << "\n";
        std::exit(1);
    }
    return argv[++i];
}

static int getArgInt(int argc, char* argv[], int& i) {
    return std::stoi(getArg(argc, argv, i));
}

static float getArgFloat(int argc, char* argv[], int& i) {
    return std::stof(getArg(argc, argv, i));
}

static uint64_t getArgU64(int argc, char* argv[], int& i) {
    return std::stoull(getArg(argc, argv, i));
}

// ── Subcommand: solve ────────────────────────────────────────────────────

static int cmdSolve(int argc, char* argv[]) {
    poker_solver::SolverConfig cfg;
    std::string resume_path;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help")       { printSolveUsage(); return 0; }
        if (arg == "--output")       cfg.output_path      = getArg(argc, argv, i);
        else if (arg == "--iterations") cfg.num_iterations = getArgInt(argc, argv, i);
        else if (arg == "--stack")      cfg.stack_depth_bb = getArgInt(argc, argv, i);
        else if (arg == "--resume")     resume_path        = getArg(argc, argv, i);
        else if (arg == "--checkpoint") cfg.checkpoint_every = getArgInt(argc, argv, i);
        else if (arg == "--alpha")      cfg.dcfr_alpha     = getArgFloat(argc, argv, i);
        else if (arg == "--beta")       cfg.dcfr_beta      = getArgFloat(argc, argv, i);
        else if (arg == "--gamma")      cfg.dcfr_gamma     = getArgFloat(argc, argv, i);
        else if (arg == "--seed")       cfg.seed           = getArgU64(argc, argv, i);
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            printSolveUsage();
            return 1;
        }
    }

    if (!resume_path.empty())
        poker_solver::solve_resume(cfg, resume_path);
    else
        poker_solver::solve(cfg);

    return 0;
}

// ── Subcommand: query ────────────────────────────────────────────────────

static int cmdQuery(int argc, char* argv[]) {
    poker_solver::SolverConfig cfg;
    std::string strategy_dir, hand, board, history;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help")      { printQueryUsage(); return 0; }
        if (arg == "--strategy")    strategy_dir       = getArg(argc, argv, i);
        else if (arg == "--hand")   hand               = getArg(argc, argv, i);
        else if (arg == "--board")  board              = getArg(argc, argv, i);
        else if (arg == "--history") history            = getArg(argc, argv, i);
        else if (arg == "--stack")  cfg.stack_depth_bb = getArgInt(argc, argv, i);
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            printQueryUsage();
            return 1;
        }
    }

    if (strategy_dir.empty() || hand.empty()) {
        std::cerr << "Error: --strategy and --hand are required\n";
        printQueryUsage();
        return 1;
    }

    auto result = poker_solver::query(strategy_dir, hand, board, history, cfg);

    if (result.actions.empty()) {
        std::cout << "No actions at this node (terminal or unreachable).\n";
        return 0;
    }

    std::cout << "Strategy for " << hand;
    if (!board.empty()) std::cout << " on " << board;
    if (!history.empty()) std::cout << " after " << history;
    std::cout << ":\n\n";

    for (size_t i = 0; i < result.actions.size(); ++i) {
        std::cout << "  " << result.actions[i] << ": "
                  << (result.probabilities[i] * 100.0f) << "%\n";
    }

    return 0;
}

// ── Subcommand: exploit ──────────────────────────────────────────────────

static int cmdExploit(int argc, char* argv[]) {
    poker_solver::SolverConfig cfg;
    std::string strategy_dir;
    int samples = 100000;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help")       { printExploitUsage(); return 0; }
        if (arg == "--strategy")     strategy_dir       = getArg(argc, argv, i);
        else if (arg == "--samples") samples            = getArgInt(argc, argv, i);
        else if (arg == "--stack")   cfg.stack_depth_bb = getArgInt(argc, argv, i);
        else if (arg == "--seed")    cfg.seed           = getArgU64(argc, argv, i);
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            printExploitUsage();
            return 1;
        }
    }

    if (strategy_dir.empty()) {
        std::cerr << "Error: --strategy is required\n";
        printExploitUsage();
        return 1;
    }

    auto r = poker_solver::measure_exploitability(strategy_dir, samples, cfg);

    std::cout << "Exploitability: " << r.exploitability << " chips/hand\n"
              << "  BR value P0:  " << r.br_value_p0 << "\n"
              << "  BR value P1:  " << r.br_value_p1 << "\n"
              << "  Samples:      " << r.num_samples << "\n";

    return 0;
}

// ── Subcommand: subgame ──────────────────────────────────────────────────

static int cmdSubgame(int argc, char* argv[]) {
    poker_solver::SolverConfig cfg;
    std::string hand, board, history;
    int iterations = 10000;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help")          { printSubgameUsage(); return 0; }
        if (arg == "--hand")            hand               = getArg(argc, argv, i);
        else if (arg == "--board")      board              = getArg(argc, argv, i);
        else if (arg == "--history")    history             = getArg(argc, argv, i);
        else if (arg == "--iterations") iterations          = getArgInt(argc, argv, i);
        else if (arg == "--stack")      cfg.stack_depth_bb  = getArgInt(argc, argv, i);
        else if (arg == "--seed")       cfg.seed            = getArgU64(argc, argv, i);
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            printSubgameUsage();
            return 1;
        }
    }

    if (hand.empty() || board.empty()) {
        std::cerr << "Error: --hand and --board are required\n";
        printSubgameUsage();
        return 1;
    }

    auto result = poker_solver::solve_subgame(hand, board, history,
                                               iterations, cfg);

    std::cout << "Subgame solution for " << hand << " on " << board;
    if (!history.empty()) std::cout << " after " << history;
    std::cout << " (" << iterations << " iterations):\n\n";

    for (size_t i = 0; i < result.actions.size(); ++i) {
        std::cout << "  " << result.actions[i] << ": "
                  << (result.probabilities[i] * 100.0f) << "%\n";
    }

    return 0;
}

// ── Subcommand: ui ──────────────────────────────────────────────────────

static void printUiUsage() {
    std::cout <<
        "Usage: poker-solver ui [options]\n"
        "\n"
        "Options:\n"
        "  --port N             Port to listen on (default: 8080)\n"
        "  --static-dir DIR     Directory for static frontend files (default: web/dist)\n";
}

static int cmdUi(int argc, char* argv[]) {
    int port = 8080;
    std::string static_dir = "web/dist";

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help")          { printUiUsage(); return 0; }
        if (arg == "--port")            port       = getArgInt(argc, argv, i);
        else if (arg == "--static-dir") static_dir = getArg(argc, argv, i);
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUiUsage();
            return 1;
        }
    }

    poker_solver::HttpServer server(port, static_dir);
    server.run();
    return 0;
}

// ── main ─────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "solve")   return cmdSolve(argc, argv);
    if (cmd == "query")   return cmdQuery(argc, argv);
    if (cmd == "exploit") return cmdExploit(argc, argv);
    if (cmd == "subgame") return cmdSubgame(argc, argv);
    if (cmd == "ui")      return cmdUi(argc, argv);
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    printUsage();
    return 1;
}
