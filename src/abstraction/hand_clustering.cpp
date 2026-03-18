#include "abstraction/hand_clustering.h"
#include "abstraction/equity_calculator.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numeric>

// ── hand index: triangular mapping for C(52,2) ──────────────────────────
int RiverBuckets::handIndex(Card c1, Card c2) {
    assert(c1 != c2 && c1 < kNumCards && c2 < kNumCards);
    if (c1 > c2) std::swap(c1, c2);
    return c1 * (2 * kNumCards - c1 - 3) / 2 + c2 - 1;
}

// ── k-means++ initialization (1D, L2) ────────────────────────────────────
static std::vector<double> kmeansppInit(const std::vector<double>& data,
                                         int k, std::mt19937_64& rng) {
    int n = static_cast<int>(data.size());
    assert(n > 0 && k > 0 && k <= n);

    std::vector<double> centroids;
    centroids.reserve(k);

    std::uniform_int_distribution<int> first_dist(0, n - 1);
    centroids.push_back(data[first_dist(rng)]);

    std::vector<double> min_dist(n, std::numeric_limits<double>::max());

    for (int c = 1; c < k; ++c) {
        double last = centroids.back();
        double total_weight = 0;
        for (int i = 0; i < n; ++i) {
            double d = (data[i] - last) * (data[i] - last);
            if (d < min_dist[i]) min_dist[i] = d;
            total_weight += min_dist[i];
        }

        std::uniform_real_distribution<double> sample(0.0, total_weight);
        double r = sample(rng);
        double cum = 0;
        int chosen = n - 1;
        for (int i = 0; i < n; ++i) {
            cum += min_dist[i];
            if (cum >= r) { chosen = i; break; }
        }
        centroids.push_back(data[chosen]);
    }

    return centroids;
}

// ── k-means L2 on 1D data ────────────────────────────────────────────────
ClusterResult kmeansL2(const std::vector<double>& data,
                       int k, int max_iter,
                       std::mt19937_64& rng, double tolerance) {
    int n = static_cast<int>(data.size());
    if (k > n) k = n;

    ClusterResult result;
    result.num_clusters = k;
    result.num_points = n;
    result.assignments.resize(n, 0);
    result.centroids = kmeansppInit(data, k, rng);

    for (int iter = 0; iter < max_iter; ++iter) {
        for (int i = 0; i < n; ++i) {
            double best_dist = std::numeric_limits<double>::max();
            int best_c = 0;
            for (int c = 0; c < k; ++c) {
                double d = (data[i] - result.centroids[c]) *
                           (data[i] - result.centroids[c]);
                if (d < best_dist) { best_dist = d; best_c = c; }
            }
            result.assignments[i] = best_c;
        }

        std::vector<double> new_centroids(k, 0.0);
        std::vector<int> counts(k, 0);
        for (int i = 0; i < n; ++i) {
            int c = result.assignments[i];
            new_centroids[c] += data[i];
            counts[c]++;
        }
        for (int c = 0; c < k; ++c) {
            if (counts[c] > 0) new_centroids[c] /= counts[c];
            else new_centroids[c] = result.centroids[c];
        }

        double max_shift = 0;
        for (int c = 0; c < k; ++c) {
            double shift = std::abs(new_centroids[c] - result.centroids[c]);
            if (shift > max_shift) max_shift = shift;
        }
        result.centroids = new_centroids;
        if (max_shift < tolerance) break;
    }

    result.total_cost = 0;
    for (int i = 0; i < n; ++i) {
        double d = data[i] - result.centroids[result.assignments[i]];
        result.total_cost += d * d;
    }
    return result;
}

// ── 1D Earth Mover's Distance ────────────────────────────────────────────
double emd1d(const double* a, const double* b, int n) {
    double cum = 0;
    double total = 0;
    for (int i = 0; i < n; ++i) {
        cum += a[i] - b[i];
        total += std::abs(cum);
    }
    return total;
}

// ── k-means++ initialization for histograms (using EMD) ──────────────────
static std::vector<std::vector<double>> kmeansppInitEMD(
    const std::vector<std::vector<double>>& data,
    int k, std::mt19937_64& rng) {

    int n = static_cast<int>(data.size());
    int dim = static_cast<int>(data[0].size());
    assert(n > 0 && k > 0 && k <= n);

    std::vector<std::vector<double>> centroids;
    centroids.reserve(k);

    std::uniform_int_distribution<int> first_dist(0, n - 1);
    centroids.push_back(data[first_dist(rng)]);

    std::vector<double> min_dist(n, std::numeric_limits<double>::max());

    for (int c = 1; c < k; ++c) {
        const auto& last = centroids.back();
        double total_weight = 0;
        for (int i = 0; i < n; ++i) {
            double d = emd1d(data[i].data(), last.data(), dim);
            double d2 = d * d;  // weight by distance squared (k-means++ standard)
            if (d2 < min_dist[i]) min_dist[i] = d2;
            total_weight += min_dist[i];
        }

        std::uniform_real_distribution<double> sample(0.0, total_weight);
        double r = sample(rng);
        double cum = 0;
        int chosen = n - 1;
        for (int i = 0; i < n; ++i) {
            cum += min_dist[i];
            if (cum >= r) { chosen = i; break; }
        }
        centroids.push_back(data[chosen]);
    }

    return centroids;
}

// ── k-means with EMD ─────────────────────────────────────────────────────
ClusterResult kmeansEMD(const std::vector<std::vector<double>>& histograms,
                        int k, int max_iter,
                        std::mt19937_64& rng, double tolerance) {
    int n = static_cast<int>(histograms.size());
    if (n == 0) return {};
    int dim = static_cast<int>(histograms[0].size());
    if (k > n) k = n;

    // Initialize centroids with k-means++.
    auto centroid_hists = kmeansppInitEMD(histograms, k, rng);

    ClusterResult result;
    result.num_clusters = k;
    result.num_points = n;
    result.assignments.resize(n, 0);

    for (int iter = 0; iter < max_iter; ++iter) {
        // ── Assignment step: assign each histogram to nearest centroid by EMD.
        for (int i = 0; i < n; ++i) {
            double best_dist = std::numeric_limits<double>::max();
            int best_c = 0;
            for (int c = 0; c < k; ++c) {
                double d = emd1d(histograms[i].data(),
                                 centroid_hists[c].data(), dim);
                if (d < best_dist) { best_dist = d; best_c = c; }
            }
            result.assignments[i] = best_c;
        }

        // ── Update step: arithmetic mean of histograms per cluster.
        std::vector<std::vector<double>> new_centroids(k, std::vector<double>(dim, 0.0));
        std::vector<int> counts(k, 0);
        for (int i = 0; i < n; ++i) {
            int c = result.assignments[i];
            for (int d = 0; d < dim; ++d)
                new_centroids[c][d] += histograms[i][d];
            counts[c]++;
        }
        for (int c = 0; c < k; ++c) {
            if (counts[c] > 0) {
                for (int d = 0; d < dim; ++d)
                    new_centroids[c][d] /= counts[c];
            } else {
                new_centroids[c] = centroid_hists[c];
            }
        }

        // Check convergence: max centroid EMD shift.
        double max_shift = 0;
        for (int c = 0; c < k; ++c) {
            double shift = emd1d(new_centroids[c].data(),
                                  centroid_hists[c].data(), dim);
            if (shift > max_shift) max_shift = shift;
        }
        centroid_hists = std::move(new_centroids);
        if (max_shift < tolerance) break;
    }

    // Store centroids as flat vector (dim entries per centroid).
    result.centroids.resize(k * dim);
    for (int c = 0; c < k; ++c)
        for (int d = 0; d < dim; ++d)
            result.centroids[c * dim + d] = centroid_hists[c][d];

    // Compute total EMD cost.
    result.total_cost = 0;
    for (int i = 0; i < n; ++i) {
        int c = result.assignments[i];
        result.total_cost += emd1d(histograms[i].data(),
                                    centroid_hists[c].data(), dim);
    }

    return result;
}

// ── Assign equity to nearest centroid ────────────────────────────────────
int assignToNearestCentroid(double equity,
                            const std::vector<double>& centroids) {
    int best = 0;
    double best_dist = std::numeric_limits<double>::max();
    for (int c = 0; c < static_cast<int>(centroids.size()); ++c) {
        double d = std::abs(equity - centroids[c]);
        if (d < best_dist) { best_dist = d; best = c; }
    }
    return best;
}

// ── Build river buckets ──────────────────────────────────────────────────
RiverBucketsResult buildRiverBuckets(int num_board_samples,
                                      int num_buckets,
                                      uint64_t seed) {
    std::mt19937_64 rng(seed);
    EquityCalculator calc;

    std::vector<double> equity_sum(RiverBuckets::kNumHands, 0.0);
    std::vector<int>    equity_count(RiverBuckets::kNumHands, 0);

    std::vector<Card> deck(kNumCards);
    std::iota(deck.begin(), deck.end(), Card(0));

    for (int b = 0; b < num_board_samples; ++b) {
        for (int i = 0; i < 5; ++i) {
            std::uniform_int_distribution<int> dist(i, kNumCards - 1);
            std::swap(deck[i], deck[dist(rng)]);
        }
        std::array<Card, 5> board = {deck[0], deck[1], deck[2],
                                      deck[3], deck[4]};

        auto results = calc.allRiverEquities(board);
        for (const auto& he : results) {
            int idx = RiverBuckets::handIndex(he.hole[0], he.hole[1]);
            equity_sum[idx] += he.equity;
            equity_count[idx]++;
        }
    }

    std::vector<double> avg_equity(RiverBuckets::kNumHands, 0.5);
    for (int i = 0; i < RiverBuckets::kNumHands; ++i) {
        if (equity_count[i] > 0)
            avg_equity[i] = equity_sum[i] / equity_count[i];
    }

    auto cr = kmeansL2(avg_equity, num_buckets, 100, rng);

    // Build sorted centroid mapping for consistent bucket ordering.
    std::vector<int> order(num_buckets);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return cr.centroids[a] < cr.centroids[b];
    });

    // Map old cluster id -> sorted rank.
    std::vector<int> rank_map(num_buckets);
    for (int i = 0; i < num_buckets; ++i)
        rank_map[order[i]] = i;

    // Build sorted centroids.
    std::vector<double> sorted_centroids(num_buckets);
    for (int i = 0; i < num_buckets; ++i)
        sorted_centroids[i] = cr.centroids[order[i]];

    RiverBucketsResult result;
    result.buckets.num_buckets = num_buckets;
    result.buckets.bucket_for_hand.resize(RiverBuckets::kNumHands);
    for (int i = 0; i < RiverBuckets::kNumHands; ++i)
        result.buckets.bucket_for_hand[i] = rank_map[cr.assignments[i]];
    result.centroids = sorted_centroids;
    result.centroid_order = order;
    return result;
}

// ── Build turn buckets ───────────────────────────────────────────────────
TurnBucketsResult buildTurnBuckets(const std::vector<double>& river_centroids,
                                    int num_river_buckets,
                                    int num_turn_buckets,
                                    int num_board_samples,
                                    uint64_t seed) {
    std::mt19937_64 rng(seed);
    EquityCalculator calc;

    int dim = num_river_buckets;

    // Accumulate histogram per hand across sampled boards.
    // hist_sum[hand_idx] is a histogram of size dim.
    std::vector<std::vector<double>> hist_sum(
        RiverBuckets::kNumHands, std::vector<double>(dim, 0.0));
    std::vector<int> hist_count(RiverBuckets::kNumHands, 0);

    std::vector<Card> deck(kNumCards);
    std::iota(deck.begin(), deck.end(), Card(0));

    for (int b = 0; b < num_board_samples; ++b) {
        // Sample a random 4-card turn board.
        for (int i = 0; i < 4; ++i) {
            std::uniform_int_distribution<int> dist(i, kNumCards - 1);
            std::swap(deck[i], deck[dist(rng)]);
        }
        std::array<Card, 4> turn_board = {deck[0], deck[1], deck[2], deck[3]};
        CardMask board_mask = cardBit(turn_board[0]) | cardBit(turn_board[1]) |
                              cardBit(turn_board[2]) | cardBit(turn_board[3]);

        // For each valid hole card combo on this turn board:
        for (int c1 = 0; c1 < kNumCards; ++c1) {
            if (board_mask & cardBit(c1)) continue;
            for (int c2 = c1 + 1; c2 < kNumCards; ++c2) {
                if (board_mask & cardBit(c2)) continue;

                std::array<Card, 2> hole = {Card(c1), Card(c2)};
                CardMask dead = board_mask | cardBit(c1) | cardBit(c2);

                // Build histogram: for each possible river card, compute
                // exact equity on the 5-card board and assign to a river bucket.
                std::vector<double> hist(dim, 0.0);
                int river_count = 0;

                for (int rc = 0; rc < kNumCards; ++rc) {
                    if (dead & cardBit(rc)) continue;

                    std::array<Card, 5> full_board = {
                        turn_board[0], turn_board[1], turn_board[2],
                        turn_board[3], Card(rc)};

                    double eq = calc.riverEquityExact(hole, full_board);
                    int bucket = assignToNearestCentroid(eq, river_centroids);
                    hist[bucket] += 1.0;
                    river_count++;
                }

                // Normalize histogram.
                if (river_count > 0) {
                    for (int d = 0; d < dim; ++d)
                        hist[d] /= river_count;
                }

                int idx = RiverBuckets::handIndex(c1, c2);
                for (int d = 0; d < dim; ++d)
                    hist_sum[idx][d] += hist[d];
                hist_count[idx]++;
            }
        }
    }

    // Average histograms and normalize.
    std::vector<std::vector<double>> avg_hist(RiverBuckets::kNumHands);
    for (int i = 0; i < RiverBuckets::kNumHands; ++i) {
        avg_hist[i].resize(dim, 0.0);
        if (hist_count[i] > 0) {
            double sum = 0;
            for (int d = 0; d < dim; ++d) {
                avg_hist[i][d] = hist_sum[i][d] / hist_count[i];
                sum += avg_hist[i][d];
            }
            // Re-normalize to sum to 1.0.
            if (sum > 0) {
                for (int d = 0; d < dim; ++d)
                    avg_hist[i][d] /= sum;
            }
        } else {
            // Unvisited hand: uniform histogram.
            for (int d = 0; d < dim; ++d)
                avg_hist[i][d] = 1.0 / dim;
        }
    }

    // Cluster using k-means EMD.
    auto cr = kmeansEMD(avg_hist, num_turn_buckets, 100, rng);

    TurnBucketsResult result;
    result.buckets.bucket_for_hand = std::move(cr.assignments);
    result.buckets.num_buckets = num_turn_buckets;

    // Extract centroid histograms.
    result.centroids.resize(num_turn_buckets);
    for (int c = 0; c < num_turn_buckets; ++c) {
        result.centroids[c].resize(dim);
        for (int d = 0; d < dim; ++d)
            result.centroids[c][d] = cr.centroids[c * dim + d];
    }

    return result;
}

// ── Build flop buckets ──────────────────────────────────────────────────
FlopBucketsResult buildFlopBuckets(const std::vector<double>& river_centroids,
                                    int num_river_buckets,
                                    int num_flop_buckets,
                                    int num_board_samples,
                                    int num_runouts_per_hand,
                                    uint64_t seed) {
    std::mt19937_64 rng(seed);
    EquityCalculator calc;

    int dim = num_river_buckets;

    // Accumulate histogram per hand across sampled flop boards.
    std::vector<std::vector<double>> hist_sum(
        RiverBuckets::kNumHands, std::vector<double>(dim, 0.0));
    std::vector<int> hist_count(RiverBuckets::kNumHands, 0);

    std::vector<Card> deck(kNumCards);
    std::iota(deck.begin(), deck.end(), Card(0));

    for (int b = 0; b < num_board_samples; ++b) {
        // Sample a random 3-card flop.
        for (int i = 0; i < 3; ++i) {
            std::uniform_int_distribution<int> dist(i, kNumCards - 1);
            std::swap(deck[i], deck[dist(rng)]);
        }
        std::array<Card, 3> flop = {deck[0], deck[1], deck[2]};
        CardMask flop_mask = cardBit(flop[0]) | cardBit(flop[1]) |
                             cardBit(flop[2]);

        // For each valid hole card combo on this flop:
        for (int c1 = 0; c1 < kNumCards; ++c1) {
            if (flop_mask & cardBit(c1)) continue;
            for (int c2 = c1 + 1; c2 < kNumCards; ++c2) {
                if (flop_mask & cardBit(c2)) continue;

                std::array<Card, 2> hole = {Card(c1), Card(c2)};
                CardMask dead = flop_mask | cardBit(c1) | cardBit(c2);

                // Build list of remaining cards for MC sampling.
                std::vector<Card> remaining;
                remaining.reserve(kNumCards);
                for (int rc = 0; rc < kNumCards; ++rc) {
                    if (!(dead & cardBit(rc)))
                        remaining.push_back(Card(rc));
                }
                int rem_size = static_cast<int>(remaining.size());

                // Sample random turn+river runouts.
                std::vector<double> hist(dim, 0.0);
                int actual_runouts = 0;

                for (int r = 0; r < num_runouts_per_hand; ++r) {
                    // Fisher-Yates partial shuffle: pick 2 cards (turn + river).
                    for (int i = 0; i < 2; ++i) {
                        std::uniform_int_distribution<int> dist(i, rem_size - 1);
                        std::swap(remaining[i], remaining[dist(rng)]);
                    }

                    std::array<Card, 5> full_board = {
                        flop[0], flop[1], flop[2],
                        remaining[0], remaining[1]};

                    double eq = calc.riverEquityExact(hole, full_board);
                    int bucket = assignToNearestCentroid(eq, river_centroids);
                    hist[bucket] += 1.0;
                    actual_runouts++;
                }

                // Normalize histogram.
                if (actual_runouts > 0) {
                    for (int d = 0; d < dim; ++d)
                        hist[d] /= actual_runouts;
                }

                int idx = RiverBuckets::handIndex(c1, c2);
                for (int d = 0; d < dim; ++d)
                    hist_sum[idx][d] += hist[d];
                hist_count[idx]++;
            }
        }
    }

    // Average histograms and normalize.
    std::vector<std::vector<double>> avg_hist(RiverBuckets::kNumHands);
    for (int i = 0; i < RiverBuckets::kNumHands; ++i) {
        avg_hist[i].resize(dim, 0.0);
        if (hist_count[i] > 0) {
            double sum = 0;
            for (int d = 0; d < dim; ++d) {
                avg_hist[i][d] = hist_sum[i][d] / hist_count[i];
                sum += avg_hist[i][d];
            }
            if (sum > 0) {
                for (int d = 0; d < dim; ++d)
                    avg_hist[i][d] /= sum;
            }
        } else {
            for (int d = 0; d < dim; ++d)
                avg_hist[i][d] = 1.0 / dim;
        }
    }

    // Cluster using k-means EMD.
    auto cr = kmeansEMD(avg_hist, num_flop_buckets, 100, rng);

    FlopBucketsResult result;
    result.buckets.bucket_for_hand = std::move(cr.assignments);
    result.buckets.num_buckets = num_flop_buckets;

    result.centroids.resize(num_flop_buckets);
    for (int c = 0; c < num_flop_buckets; ++c) {
        result.centroids[c].resize(dim);
        for (int d = 0; d < dim; ++d)
            result.centroids[c][d] = cr.centroids[c * dim + d];
    }

    return result;
}

// ── Preflop buckets ─────────────────────────────────────────────────────
int PreflopBuckets::preflopClass(Card c1, Card c2) {
    return canonicalPreflopHand(c1, c2);
}

PreflopBuckets buildPreflopBuckets() {
    PreflopBuckets result;
    result.num_buckets = PreflopBuckets::kNumClasses;
    result.bucket_for_hand.resize(RiverBuckets::kNumHands);

    for (int c1 = 0; c1 < kNumCards; ++c1) {
        for (int c2 = c1 + 1; c2 < kNumCards; ++c2) {
            int idx = RiverBuckets::handIndex(c1, c2);
            result.bucket_for_hand[idx] = canonicalPreflopHand(c1, c2);
        }
    }

    return result;
}
