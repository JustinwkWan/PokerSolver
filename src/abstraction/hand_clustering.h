#pragma once

#include "core/cards.h"

#include <cstdint>
#include <random>
#include <vector>

// ── ClusterResult ─────────────────────────────────────────────────────────
struct ClusterResult {
    std::vector<int> assignments;    // assignments[i] = cluster_id for point i
    std::vector<double> centroids;   // centroids[k] (1D) or centroids[k*dim..] (ND)
    int num_clusters;
    int num_points;
    double total_cost;               // sum of squared distances to centroid
};

// ── k-means with L2 distance on 1D data ──────────────────────────────────
// Uses k-means++ initialization + Lloyd's algorithm.
ClusterResult kmeansL2(const std::vector<double>& data,
                       int k,
                       int max_iter,
                       std::mt19937_64& rng,
                       double tolerance = 1e-6);

// ── k-means with Earth Mover's Distance (for turn/flop clustering) ──────
// Each point is a histogram of `dim` bins (should sum to 1.0).
// Uses k-means++ init with EMD, Lloyd's with arithmetic-mean centroids.
ClusterResult kmeansEMD(const std::vector<std::vector<double>>& histograms,
                        int k,
                        int max_iter,
                        std::mt19937_64& rng,
                        double tolerance = 1e-6);

// ── 1D Earth Mover's Distance ────────────────────────────────────────────
// Exact O(n) computation for two histograms of equal length.
double emd1d(const double* a, const double* b, int n);

// ── River bucket assignment ──────────────────────────────────────────────
struct RiverBuckets {
    // bucket_for_hand[i] = bucket_id for the i-th hole card pair.
    // Index via handIndex(c1, c2) where c1 < c2.
    std::vector<int> bucket_for_hand;
    int num_buckets;

    static int handIndex(Card c1, Card c2);
    static constexpr int kNumHands = 1326;  // C(52,2)
};

// Build river buckets:
//   1. Sample num_board_samples random 5-card boards
//   2. For each board, compute exact equity for all valid hole combos
//   3. Average equity per hand across all boards
//   4. Cluster the 1326 hands into num_buckets buckets using k-means L2
//
// Returns both bucket assignments AND centroids (needed for turn histograms).
struct RiverBucketsResult {
    RiverBuckets buckets;
    std::vector<double> centroids;  // sorted equity centroids [num_buckets]
    std::vector<int> centroid_order; // maps original cluster id -> sorted rank
};

RiverBucketsResult buildRiverBuckets(int num_board_samples,
                                      int num_buckets,
                                      uint64_t seed);

// ── Turn bucket assignment ───────────────────────────────────────────────
struct TurnBuckets {
    std::vector<int> bucket_for_hand;
    int num_buckets;
};

// Assign a single river equity value to the nearest river centroid.
int assignToNearestCentroid(double equity,
                            const std::vector<double>& centroids);

// Build turn buckets:
//   1. Sample num_board_samples random 4-card boards
//   2. For each board + each valid hole combo, enumerate all river cards
//   3. For each resulting 5-card board, compute exact equity → river bucket
//   4. Build histogram over river buckets for each hand
//   5. Average histograms per hand across sampled boards
//   6. Cluster using k-means EMD → turn buckets
//
// Requires river centroids from buildRiverBuckets.
struct TurnBucketsResult {
    TurnBuckets buckets;
    std::vector<std::vector<double>> centroids;  // [num_buckets][num_river_buckets]
};

TurnBucketsResult buildTurnBuckets(const std::vector<double>& river_centroids,
                                    int num_river_buckets,
                                    int num_turn_buckets,
                                    int num_board_samples,
                                    uint64_t seed);

// ── Flop bucket assignment ─────────────────────────────────────────────
struct FlopBuckets {
    std::vector<int> bucket_for_hand;
    int num_buckets;
};

// Build flop buckets:
//   1. Sample num_board_samples random 3-card flop boards
//   2. For each flop + each valid hole combo, sample num_runouts turn+river
//      runouts via Monte Carlo
//   3. For each 5-card runout, compute exact equity → assign to river bucket
//   4. Build histogram over river buckets for each hand
//   5. Average histograms per hand across sampled flops
//   6. Cluster using k-means EMD → flop buckets
//
// Requires river centroids from buildRiverBuckets.
struct FlopBucketsResult {
    FlopBuckets buckets;
    std::vector<std::vector<double>> centroids;  // [num_buckets][num_river_buckets]
};

FlopBucketsResult buildFlopBuckets(const std::vector<double>& river_centroids,
                                    int num_river_buckets,
                                    int num_flop_buckets,
                                    int num_board_samples,
                                    int num_runouts_per_hand,
                                    uint64_t seed);

// ── Preflop bucket assignment ──────────────────────────────────────────
// 169 canonical preflop hands (13 pairs + 78 suited + 78 offsuit).
// Assigned by lookup table — no clustering needed.
struct PreflopBuckets {
    std::vector<int> bucket_for_hand;  // indexed by handIndex(c1, c2)
    int num_buckets;                   // always 169

    // Map a hole card pair to its canonical preflop class [0..168].
    static int preflopClass(Card c1, Card c2);
    static constexpr int kNumClasses = 169;
};

PreflopBuckets buildPreflopBuckets();
