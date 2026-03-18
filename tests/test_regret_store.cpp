#include "solver/regret_store.h"

#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <string>

namespace fs = std::filesystem;

// Helper: create a temporary directory for mmap tests.
static std::string makeTempDir() {
    auto path = fs::temp_directory_path() / "regret_store_test_XXXXXX";
    std::string s = path.string();
    char* result = mkdtemp(s.data());
    if (!result) throw std::runtime_error("mkdtemp failed");
    return s;
}

static void cleanupDir(const std::string& dir) {
    fs::remove_all(dir);
}

// ── Anonymous mode: basic construction ───────────────────────────────────

TEST(RegretStore, AnonymousConstruction) {
    RegretStore store(100, 4);
    EXPECT_EQ(store.numInfoSets(), 100u);
    EXPECT_EQ(store.maxActions(), 4);
    EXPECT_EQ(store.totalEntries(), 400u);
}

// ── Anonymous mode: zero-initialized ─────────────────────────────────────

TEST(RegretStore, AnonymousZeroInitialized) {
    RegretStore store(50, 3);
    for (uint64_t i = 0; i < store.totalEntries(); ++i) {
        EXPECT_FLOAT_EQ(store.regrets(0)[i], 0.0f);
        EXPECT_FLOAT_EQ(store.regrets(1)[i], 0.0f);
        EXPECT_FLOAT_EQ(store.strategy(0)[i], 0.0f);
        EXPECT_FLOAT_EQ(store.strategy(1)[i], 0.0f);
    }
}

// ── Read/write regrets ───────────────────────────────────────────────────

TEST(RegretStore, ReadWriteRegrets) {
    RegretStore store(10, 3);

    // Write some regrets for player 0, info set 5.
    float* r = store.regretSlice(0, 5);
    r[0] = 1.5f;
    r[1] = -0.5f;
    r[2] = 3.0f;

    // Read them back.
    const float* cr = store.regretSlice(0, 5);
    EXPECT_FLOAT_EQ(cr[0], 1.5f);
    EXPECT_FLOAT_EQ(cr[1], -0.5f);
    EXPECT_FLOAT_EQ(cr[2], 3.0f);

    // Player 1's regrets at same info set should still be zero.
    const float* cr1 = store.regretSlice(1, 5);
    EXPECT_FLOAT_EQ(cr1[0], 0.0f);
}

// ── Regret matching: positive regrets ────────────────────────────────────

TEST(RegretStore, RegretMatchingPositive) {
    RegretStore store(10, 3);

    float* r = store.regretSlice(0, 0);
    r[0] = 2.0f;
    r[1] = 1.0f;
    r[2] = 1.0f;

    float strat[3];
    store.getStrategy(0, 0, 3, strat);

    EXPECT_FLOAT_EQ(strat[0], 0.5f);
    EXPECT_FLOAT_EQ(strat[1], 0.25f);
    EXPECT_FLOAT_EQ(strat[2], 0.25f);
}

// ── Regret matching: all negative → uniform ──────────────────────────────

TEST(RegretStore, RegretMatchingAllNegative) {
    RegretStore store(10, 3);

    float* r = store.regretSlice(0, 0);
    r[0] = -1.0f;
    r[1] = -2.0f;
    r[2] = -3.0f;

    float strat[3];
    store.getStrategy(0, 0, 3, strat);

    float expected = 1.0f / 3.0f;
    EXPECT_NEAR(strat[0], expected, 1e-6f);
    EXPECT_NEAR(strat[1], expected, 1e-6f);
    EXPECT_NEAR(strat[2], expected, 1e-6f);
}

// ── Regret matching: mixed positive/negative ─────────────────────────────

TEST(RegretStore, RegretMatchingMixed) {
    RegretStore store(10, 4);

    float* r = store.regretSlice(1, 3);
    r[0] = -5.0f;
    r[1] = 3.0f;
    r[2] = 0.0f;
    r[3] = 7.0f;

    float strat[4];
    store.getStrategy(1, 3, 4, strat);

    // Positive regrets: 0, 3, 0, 7. Sum = 10.
    EXPECT_FLOAT_EQ(strat[0], 0.0f);
    EXPECT_FLOAT_EQ(strat[1], 0.3f);
    EXPECT_FLOAT_EQ(strat[2], 0.0f);
    EXPECT_FLOAT_EQ(strat[3], 0.7f);
}

// ── Average strategy: with accumulated strategy sums ─────────────────────

TEST(RegretStore, AverageStrategy) {
    RegretStore store(10, 3);

    float* s = store.strategySlice(0, 2);
    s[0] = 100.0f;
    s[1] = 200.0f;
    s[2] = 300.0f;

    float avg[3];
    store.getAverageStrategy(0, 2, 3, avg);

    float total = 600.0f;
    EXPECT_NEAR(avg[0], 100.0f / total, 1e-6f);
    EXPECT_NEAR(avg[1], 200.0f / total, 1e-6f);
    EXPECT_NEAR(avg[2], 300.0f / total, 1e-6f);
}

// ── Average strategy: all zero → uniform ─────────────────────────────────

TEST(RegretStore, AverageStrategyZero) {
    RegretStore store(10, 3);

    float avg[3];
    store.getAverageStrategy(0, 0, 3, avg);

    float expected = 1.0f / 3.0f;
    EXPECT_NEAR(avg[0], expected, 1e-6f);
    EXPECT_NEAR(avg[1], expected, 1e-6f);
    EXPECT_NEAR(avg[2], expected, 1e-6f);
}

// ── Strategy probabilities sum to 1 ─────────────────────────────────────

TEST(RegretStore, StrategyProbabilitySumOne) {
    RegretStore store(20, 5);

    // Set random-ish regrets.
    float* r = store.regretSlice(0, 7);
    r[0] = 10.0f;
    r[1] = -3.0f;
    r[2] = 5.0f;
    r[3] = 0.0f;
    r[4] = 2.0f;

    float strat[5];
    store.getStrategy(0, 7, 5, strat);

    float sum = 0.0f;
    for (int a = 0; a < 5; ++a) sum += strat[a];
    EXPECT_NEAR(sum, 1.0f, 1e-6f);
}

// ── Move semantics ───────────────────────────────────────────────────────

TEST(RegretStore, MoveConstruction) {
    RegretStore a(10, 3);
    a.regretSlice(0, 0)[0] = 42.0f;

    RegretStore b(std::move(a));
    EXPECT_EQ(b.numInfoSets(), 10u);
    EXPECT_FLOAT_EQ(b.regretSlice(0, 0)[0], 42.0f);
}

// ── File-backed mode: create, write, reload ──────────────────────────────

TEST(RegretStore, FileBackedCreateAndReload) {
    auto dir = makeTempDir();

    {
        RegretStore store(50, 4, dir);
        store.regretSlice(0, 10)[2] = 7.5f;
        store.strategySlice(1, 25)[0] = 99.0f;
        store.checkpoint(dir, 1000);
    }

    // Reload from checkpoint.
    auto loaded = RegretStore::load(dir);
    EXPECT_EQ(loaded.numInfoSets(), 50u);
    EXPECT_EQ(loaded.maxActions(), 4);
    EXPECT_FLOAT_EQ(loaded.regretSlice(0, 10)[2], 7.5f);
    EXPECT_FLOAT_EQ(loaded.strategySlice(1, 25)[0], 99.0f);

    cleanupDir(dir);
}

// ── File-backed: zero-initialized on creation ────────────────────────────

TEST(RegretStore, FileBackedZeroInit) {
    auto dir = makeTempDir();

    RegretStore store(20, 3, dir);

    bool all_zero = true;
    for (uint64_t i = 0; i < store.totalEntries(); ++i) {
        if (store.regrets(0)[i] != 0.0f || store.regrets(1)[i] != 0.0f ||
            store.strategy(0)[i] != 0.0f || store.strategy(1)[i] != 0.0f) {
            all_zero = false;
            break;
        }
    }
    EXPECT_TRUE(all_zero);

    cleanupDir(dir);
}

// ── Anonymous checkpoint then load ───────────────────────────────────────

TEST(RegretStore, AnonymousCheckpointAndLoad) {
    auto dir = makeTempDir();

    {
        RegretStore store(30, 5);
        store.regretSlice(1, 15)[3] = -12.5f;
        store.strategySlice(0, 0)[4] = 1000.0f;
        store.checkpoint(dir, 500);
    }

    auto loaded = RegretStore::load(dir);
    EXPECT_EQ(loaded.numInfoSets(), 30u);
    EXPECT_EQ(loaded.maxActions(), 5);
    EXPECT_FLOAT_EQ(loaded.regretSlice(1, 15)[3], -12.5f);
    EXPECT_FLOAT_EQ(loaded.strategySlice(0, 0)[4], 1000.0f);

    cleanupDir(dir);
}

// ── Total bytes calculation ──────────────────────────────────────────────

TEST(RegretStore, TotalBytesCalculation) {
    RegretStore store(100, 5);
    // 100 info sets * 5 actions * 4 bytes * 4 arrays = 8000.
    EXPECT_EQ(store.totalBytes(), 8000u);
    EXPECT_EQ(store.bytesPerArray(), 2000u);
}

// ── Slice addressing is correct ──────────────────────────────────────────

TEST(RegretStore, SliceAddressing) {
    RegretStore store(10, 4);

    // Write via slice, read via raw pointer.
    store.regretSlice(0, 3)[2] = 5.0f;
    EXPECT_FLOAT_EQ(store.regrets(0)[3 * 4 + 2], 5.0f);

    store.strategySlice(1, 7)[0] = 8.0f;
    EXPECT_FLOAT_EQ(store.strategy(1)[7 * 4 + 0], 8.0f);
}
