// build_abstraction — Offline tool to compute card bucket assignments.
//
// Runs the full abstraction pipeline:
//   1. River buckets  (k-means L2 on equity)
//   2. Turn buckets   (k-means EMD on river bucket histograms)
//   3. Flop buckets   (k-means EMD on river bucket histograms via MC runouts)
//   4. Preflop buckets (canonical 169 hand classes)
//
// Outputs binary bucket files to the specified directory.

#include "abstraction/hand_clustering.h"
#include "core/cards.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

// ── Defaults ─────────────────────────────────────────────────────────────

static constexpr int kDefaultRiverBuckets    = 2000;
static constexpr int kDefaultTurnBuckets     = 1000;
static constexpr int kDefaultFlopBuckets     = 500;
static constexpr int kDefaultRiverBoards     = 100;
static constexpr int kDefaultTurnBoards      = 100;
static constexpr int kDefaultFlopBoards      = 100;
static constexpr int kDefaultFlopRunouts     = 200;
static constexpr uint64_t kDefaultSeed       = 42;

// ── Config ───────────────────────────────────────────────────────────────

struct Config {
    int river_buckets     = kDefaultRiverBuckets;
    int turn_buckets      = kDefaultTurnBuckets;
    int flop_buckets      = kDefaultFlopBuckets;
    int river_boards      = kDefaultRiverBoards;
    int turn_boards       = kDefaultTurnBoards;
    int flop_boards       = kDefaultFlopBoards;
    int flop_runouts      = kDefaultFlopRunouts;
    uint64_t seed         = kDefaultSeed;
    std::string output_dir = "data/buckets";
};

// ── Helpers ──────────────────────────────────────────────────────────────

static void writeBucketFile(const std::string& path,
                             const std::vector<int>& buckets,
                             int num_buckets) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "Error: cannot open " << path << " for writing\n";
        std::exit(1);
    }
    // Header: num_hands (int32), num_buckets (int32).
    int32_t n = static_cast<int32_t>(buckets.size());
    int32_t nb = static_cast<int32_t>(num_buckets);
    ofs.write(reinterpret_cast<const char*>(&n), 4);
    ofs.write(reinterpret_cast<const char*>(&nb), 4);
    // Body: bucket_id per hand (int32 each).
    for (int b : buckets) {
        int32_t val = static_cast<int32_t>(b);
        ofs.write(reinterpret_cast<const char*>(&val), 4);
    }
    ofs.close();
}

static void writeCentroidFile(const std::string& path,
                               const std::vector<double>& centroids,
                               int num_buckets) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "Error: cannot open " << path << " for writing\n";
        std::exit(1);
    }
    int32_t nb = static_cast<int32_t>(num_buckets);
    int32_t dim = static_cast<int32_t>(centroids.size() / num_buckets);
    ofs.write(reinterpret_cast<const char*>(&nb), 4);
    ofs.write(reinterpret_cast<const char*>(&dim), 4);
    ofs.write(reinterpret_cast<const char*>(centroids.data()),
              centroids.size() * sizeof(double));
    ofs.close();
}

static double elapsed(std::chrono::steady_clock::time_point start) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start).count();
}

// ── Usage ────────────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n\n"
        << "Options:\n"
        << "  --output DIR          Output directory (default: data/buckets)\n"
        << "  --river-buckets N     River bucket count (default: "
            << kDefaultRiverBuckets << ")\n"
        << "  --turn-buckets N      Turn bucket count (default: "
            << kDefaultTurnBuckets << ")\n"
        << "  --flop-buckets N      Flop bucket count (default: "
            << kDefaultFlopBuckets << ")\n"
        << "  --river-boards N      Board samples for river (default: "
            << kDefaultRiverBoards << ")\n"
        << "  --turn-boards N       Board samples for turn (default: "
            << kDefaultTurnBoards << ")\n"
        << "  --flop-boards N       Board samples for flop (default: "
            << kDefaultFlopBoards << ")\n"
        << "  --flop-runouts N      MC runouts per hand for flop (default: "
            << kDefaultFlopRunouts << ")\n"
        << "  --seed N              Random seed (default: " << kDefaultSeed << ")\n"
        << "  --help                Show this help\n";
}

static Config parseArgs(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        }
        auto nextInt = [&]() -> int {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires an argument\n";
                std::exit(1);
            }
            return std::atoi(argv[++i]);
        };
        auto nextU64 = [&]() -> uint64_t {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires an argument\n";
                std::exit(1);
            }
            return std::strtoull(argv[++i], nullptr, 10);
        };
        auto nextStr = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires an argument\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "--output")         cfg.output_dir    = nextStr();
        else if (arg == "--river-buckets") cfg.river_buckets = nextInt();
        else if (arg == "--turn-buckets")  cfg.turn_buckets  = nextInt();
        else if (arg == "--flop-buckets")  cfg.flop_buckets  = nextInt();
        else if (arg == "--river-boards")  cfg.river_boards  = nextInt();
        else if (arg == "--turn-boards")   cfg.turn_boards   = nextInt();
        else if (arg == "--flop-boards")   cfg.flop_boards   = nextInt();
        else if (arg == "--flop-runouts")  cfg.flop_runouts  = nextInt();
        else if (arg == "--seed")          cfg.seed          = nextU64();
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            std::exit(1);
        }
    }
    return cfg;
}

// ── Main ─────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    Config cfg = parseArgs(argc, argv);

    initCards();

    std::cout << "=== Build Abstraction Pipeline ===\n"
              << "Output:        " << cfg.output_dir << "\n"
              << "River buckets: " << cfg.river_buckets
              << " (" << cfg.river_boards << " boards)\n"
              << "Turn buckets:  " << cfg.turn_buckets
              << " (" << cfg.turn_boards << " boards)\n"
              << "Flop buckets:  " << cfg.flop_buckets
              << " (" << cfg.flop_boards << " boards, "
              << cfg.flop_runouts << " runouts/hand)\n"
              << "Seed:          " << cfg.seed << "\n\n";

    // Create output directory.
    std::string mkdir_cmd = "mkdir -p " + cfg.output_dir;
    std::system(mkdir_cmd.c_str());

    // ── Step 1: River buckets ────────────────────────────────────────────
    std::cout << "[1/4] Building river buckets..." << std::flush;
    auto t0 = std::chrono::steady_clock::now();

    auto river = buildRiverBuckets(cfg.river_boards, cfg.river_buckets, cfg.seed);

    std::cout << " done (" << elapsed(t0) << "s)\n";
    writeBucketFile(cfg.output_dir + "/river_buckets.bin",
                    river.buckets.bucket_for_hand, river.buckets.num_buckets);
    writeCentroidFile(cfg.output_dir + "/river_centroids.bin",
                      river.centroids, cfg.river_buckets);
    std::cout << "  -> " << river.buckets.num_buckets << " buckets, "
              << river.buckets.bucket_for_hand.size() << " hands\n";

    // ── Step 2: Turn buckets ─────────────────────────────────────────────
    std::cout << "[2/4] Building turn buckets..." << std::flush;
    t0 = std::chrono::steady_clock::now();

    auto turn = buildTurnBuckets(river.centroids, cfg.river_buckets,
                                  cfg.turn_buckets, cfg.turn_boards,
                                  cfg.seed + 1);

    std::cout << " done (" << elapsed(t0) << "s)\n";
    writeBucketFile(cfg.output_dir + "/turn_buckets.bin",
                    turn.buckets.bucket_for_hand, turn.buckets.num_buckets);
    std::cout << "  -> " << turn.buckets.num_buckets << " buckets\n";

    // ── Step 3: Flop buckets ─────────────────────────────────────────────
    std::cout << "[3/4] Building flop buckets..." << std::flush;
    t0 = std::chrono::steady_clock::now();

    auto flop = buildFlopBuckets(river.centroids, cfg.river_buckets,
                                  cfg.flop_buckets, cfg.flop_boards,
                                  cfg.flop_runouts, cfg.seed + 2);

    std::cout << " done (" << elapsed(t0) << "s)\n";
    writeBucketFile(cfg.output_dir + "/flop_buckets.bin",
                    flop.buckets.bucket_for_hand, flop.buckets.num_buckets);
    std::cout << "  -> " << flop.buckets.num_buckets << " buckets\n";

    // ── Step 4: Preflop buckets ──────────────────────────────────────────
    std::cout << "[4/4] Building preflop buckets..." << std::flush;
    t0 = std::chrono::steady_clock::now();

    auto preflop = buildPreflopBuckets();

    std::cout << " done (" << elapsed(t0) << "s)\n";
    writeBucketFile(cfg.output_dir + "/preflop_buckets.bin",
                    preflop.bucket_for_hand, preflop.num_buckets);
    std::cout << "  -> " << preflop.num_buckets << " buckets (canonical)\n";

    std::cout << "\n=== Pipeline complete ===\n"
              << "Files written to: " << cfg.output_dir << "/\n";

    return 0;
}
