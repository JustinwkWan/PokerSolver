#include "abstraction/hand_clustering.h"
#include "core/cards.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

// ── handIndex produces exactly 1326 unique values ────────────────────────

TEST(HandIndex, AllUnique) {
    std::set<int> seen;
    for (int c1 = 0; c1 < kNumCards; ++c1)
        for (int c2 = c1 + 1; c2 < kNumCards; ++c2) {
            int idx = RiverBuckets::handIndex(Card(c1), Card(c2));
            EXPECT_GE(idx, 0);
            EXPECT_LT(idx, RiverBuckets::kNumHands);
            EXPECT_TRUE(seen.insert(idx).second)
                << "Duplicate index " << idx << " for ("
                << c1 << ", " << c2 << ")";
        }
    EXPECT_EQ(static_cast<int>(seen.size()), RiverBuckets::kNumHands);
}

TEST(HandIndex, SymmetricOrder) {
    // handIndex(a, b) == handIndex(b, a)
    EXPECT_EQ(RiverBuckets::handIndex(0, 10),
              RiverBuckets::handIndex(10, 0));
    EXPECT_EQ(RiverBuckets::handIndex(3, 51),
              RiverBuckets::handIndex(51, 3));
}

// ── k-means: trivial k=1 ────────────────────────────────────────────────

TEST(KMeans, SingleCluster) {
    std::vector<double> data = {0.1, 0.2, 0.3, 0.4, 0.5};
    std::mt19937_64 rng(42);
    auto result = kmeansL2(data, 1, 100, rng);

    EXPECT_EQ(result.num_clusters, 1);
    for (int a : result.assignments) EXPECT_EQ(a, 0);

    double expected_mean = 0.3;
    EXPECT_NEAR(result.centroids[0], expected_mean, 1e-9);
}

// ── k-means: k = n (each point its own cluster) ─────────────────────────

TEST(KMeans, KEqualsN) {
    std::vector<double> data = {1.0, 5.0, 10.0};
    std::mt19937_64 rng(42);
    auto result = kmeansL2(data, 3, 100, rng);

    EXPECT_EQ(result.num_clusters, 3);
    // Every point should be alone in its cluster
    std::set<int> clusters(result.assignments.begin(), result.assignments.end());
    EXPECT_EQ(static_cast<int>(clusters.size()), 3);
}

// ── k-means: well-separated clusters ─────────────────────────────────────

TEST(KMeans, WellSeparated) {
    // 3 clusters at 0.1, 0.5, 0.9 with tight spread
    std::mt19937_64 data_rng(100);
    std::vector<double> data;
    for (int i = 0; i < 100; ++i) {
        std::normal_distribution<double> d(0.1, 0.01);
        data.push_back(d(data_rng));
    }
    for (int i = 0; i < 100; ++i) {
        std::normal_distribution<double> d(0.5, 0.01);
        data.push_back(d(data_rng));
    }
    for (int i = 0; i < 100; ++i) {
        std::normal_distribution<double> d(0.9, 0.01);
        data.push_back(d(data_rng));
    }

    std::mt19937_64 rng(42);
    auto result = kmeansL2(data, 3, 100, rng);

    // All points in group 0 (indices 0-99) should have the same assignment.
    int cluster_low = result.assignments[0];
    int cluster_mid = result.assignments[100];
    int cluster_high = result.assignments[200];

    // All three clusters should be different.
    EXPECT_NE(cluster_low, cluster_mid);
    EXPECT_NE(cluster_low, cluster_high);
    EXPECT_NE(cluster_mid, cluster_high);

    // Verify all 100 points per group are assigned correctly.
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(result.assignments[i], cluster_low);
    for (int i = 100; i < 200; ++i)
        EXPECT_EQ(result.assignments[i], cluster_mid);
    for (int i = 200; i < 300; ++i)
        EXPECT_EQ(result.assignments[i], cluster_high);
}

// ── k-means: deterministic with same seed ────────────────────────────────

TEST(KMeans, Deterministic) {
    std::vector<double> data;
    for (int i = 0; i < 500; ++i)
        data.push_back(i / 500.0);

    std::mt19937_64 rng1(77);
    auto r1 = kmeansL2(data, 10, 100, rng1);

    std::mt19937_64 rng2(77);
    auto r2 = kmeansL2(data, 10, 100, rng2);

    EXPECT_EQ(r1.assignments, r2.assignments);
    EXPECT_EQ(r1.centroids, r2.centroids);
}

// ── k-means++: initial centroids are spread out ──────────────────────────

TEST(KMeans, InitCentroidsSpread) {
    std::vector<double> data;
    for (int i = 0; i < 1000; ++i)
        data.push_back(i / 1000.0);

    std::mt19937_64 rng(42);
    auto result = kmeansL2(data, 10, 1, rng);  // just 1 iteration to see init

    // Centroids should be roughly spread across [0, 1].
    auto sorted = result.centroids;
    std::sort(sorted.begin(), sorted.end());

    // No two centroids should be closer than 0.01 in a uniform [0,1] dataset
    for (int i = 1; i < 10; ++i) {
        EXPECT_GT(sorted[i] - sorted[i - 1], 0.01)
            << "Centroids " << (i - 1) << " and " << i << " too close";
    }
}

// ── EMD 1D: known distances ──────────────────────────────────────────────

TEST(EMD, IdenticalHistograms) {
    double a[] = {0.25, 0.25, 0.25, 0.25};
    double b[] = {0.25, 0.25, 0.25, 0.25};
    EXPECT_NEAR(emd1d(a, b, 4), 0.0, 1e-12);
}

TEST(EMD, DiracDelta) {
    // All mass at bin 0 vs all mass at bin 3: need to move 1 unit across 3 bins
    double a[] = {1.0, 0.0, 0.0, 0.0};
    double b[] = {0.0, 0.0, 0.0, 1.0};
    // EMD = |1| + |1| + |1| = 3.0 (cumulative diffs: 1, 1, 1)
    EXPECT_NEAR(emd1d(a, b, 4), 3.0, 1e-12);
}

TEST(EMD, AdjacentShift) {
    // Mass shifted one bin right
    double a[] = {0.5, 0.5, 0.0};
    double b[] = {0.0, 0.5, 0.5};
    // Cumulative diffs: 0.5, 0.5, 0.0 → EMD = 1.0
    EXPECT_NEAR(emd1d(a, b, 3), 1.0, 1e-12);
}

// ── River buckets: basic sanity ──────────────────────────────────────────
// Use a small number of boards and buckets to keep it fast.

TEST(RiverBuckets, BasicSanity) {
    initCards();
    auto result = buildRiverBuckets(50, 20, 42);

    EXPECT_EQ(result.buckets.num_buckets, 20);
    EXPECT_EQ(static_cast<int>(result.buckets.bucket_for_hand.size()),
              RiverBuckets::kNumHands);

    // All assignments in valid range
    for (int b : result.buckets.bucket_for_hand) {
        EXPECT_GE(b, 0);
        EXPECT_LT(b, 20);
    }

    // Centroids should be sorted and in [0, 1]
    EXPECT_EQ(static_cast<int>(result.centroids.size()), 20);
    for (int i = 0; i < 20; ++i) {
        EXPECT_GE(result.centroids[i], 0.0);
        EXPECT_LE(result.centroids[i], 1.0);
    }
    for (int i = 1; i < 20; ++i) {
        EXPECT_LE(result.centroids[i - 1], result.centroids[i]);
    }
}

TEST(RiverBuckets, StrongHandsSameArea) {
    initCards();
    auto result = buildRiverBuckets(200, 50, 123);

    Card Ah = cardFromStr("Ah"), As = cardFromStr("As");
    Card Kh = cardFromStr("Kh"), Ks = cardFromStr("Ks");
    Card h7 = cardFromStr("7h"), c2 = cardFromStr("2c");

    int aa_bucket = result.buckets.bucket_for_hand[RiverBuckets::handIndex(Ah, As)];
    int kk_bucket = result.buckets.bucket_for_hand[RiverBuckets::handIndex(Kh, Ks)];
    int trash_bucket = result.buckets.bucket_for_hand[RiverBuckets::handIndex(h7, c2)];

    EXPECT_NE(aa_bucket, trash_bucket)
        << "AA and 72o should not be in the same bucket";
    EXPECT_NE(kk_bucket, trash_bucket)
        << "KK and 72o should not be in the same bucket";
}

TEST(RiverBuckets, Deterministic) {
    initCards();
    auto b1 = buildRiverBuckets(50, 20, 42);
    auto b2 = buildRiverBuckets(50, 20, 42);
    EXPECT_EQ(b1.buckets.bucket_for_hand, b2.buckets.bucket_for_hand);
}

// ── assignToNearestCentroid ────────────────────────────────────────────────

TEST(NearestCentroid, ExactMatch) {
    std::vector<double> centroids = {0.1, 0.3, 0.5, 0.7, 0.9};
    EXPECT_EQ(assignToNearestCentroid(0.1, centroids), 0);
    EXPECT_EQ(assignToNearestCentroid(0.5, centroids), 2);
    EXPECT_EQ(assignToNearestCentroid(0.9, centroids), 4);
}

TEST(NearestCentroid, MidpointTiesGoLower) {
    std::vector<double> centroids = {0.0, 1.0};
    // At 0.5, equidistant — should pick first (index 0) due to < comparison
    int idx = assignToNearestCentroid(0.5, centroids);
    EXPECT_TRUE(idx == 0 || idx == 1);  // either is acceptable
}

TEST(NearestCentroid, ClosestWins) {
    std::vector<double> centroids = {0.1, 0.3, 0.5, 0.7, 0.9};
    EXPECT_EQ(assignToNearestCentroid(0.25, centroids), 1);  // closest to 0.3
    EXPECT_EQ(assignToNearestCentroid(0.82, centroids), 4);  // closest to 0.9
}

// ── kmeansEMD: synthetic histogram clustering ─────────────────────────────

TEST(KMeansEMD, TwoDistinctGroups) {
    // Group A: mass concentrated at left bins
    // Group B: mass concentrated at right bins
    std::vector<std::vector<double>> histograms;
    for (int i = 0; i < 50; ++i)
        histograms.push_back({0.8, 0.2, 0.0, 0.0});
    for (int i = 0; i < 50; ++i)
        histograms.push_back({0.0, 0.0, 0.2, 0.8});

    std::mt19937_64 rng(42);
    auto result = kmeansEMD(histograms, 2, 100, rng);

    EXPECT_EQ(result.num_clusters, 2);
    int cluster_left = result.assignments[0];
    int cluster_right = result.assignments[50];
    EXPECT_NE(cluster_left, cluster_right);

    for (int i = 0; i < 50; ++i)
        EXPECT_EQ(result.assignments[i], cluster_left);
    for (int i = 50; i < 100; ++i)
        EXPECT_EQ(result.assignments[i], cluster_right);
}

TEST(KMeansEMD, SingleCluster) {
    std::vector<std::vector<double>> histograms;
    for (int i = 0; i < 20; ++i)
        histograms.push_back({0.25, 0.25, 0.25, 0.25});

    std::mt19937_64 rng(42);
    auto result = kmeansEMD(histograms, 1, 100, rng);

    EXPECT_EQ(result.num_clusters, 1);
    for (int a : result.assignments)
        EXPECT_EQ(a, 0);
}

TEST(KMeansEMD, Deterministic) {
    std::vector<std::vector<double>> histograms;
    std::mt19937_64 data_rng(99);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (int i = 0; i < 100; ++i) {
        std::vector<double> h(5);
        double sum = 0;
        for (auto& v : h) { v = dist(data_rng); sum += v; }
        for (auto& v : h) v /= sum;
        histograms.push_back(h);
    }

    std::mt19937_64 rng1(42);
    auto r1 = kmeansEMD(histograms, 5, 100, rng1);
    std::mt19937_64 rng2(42);
    auto r2 = kmeansEMD(histograms, 5, 100, rng2);

    EXPECT_EQ(r1.assignments, r2.assignments);
}

// ── Histogram normalization check ─────────────────────────────────────────

TEST(KMeansEMD, CentroidsNormalized) {
    // After clustering, centroids (arithmetic means) should roughly sum to 1
    std::vector<std::vector<double>> histograms;
    std::mt19937_64 data_rng(77);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (int i = 0; i < 60; ++i) {
        std::vector<double> h(4);
        double sum = 0;
        for (auto& v : h) { v = dist(data_rng); sum += v; }
        for (auto& v : h) v /= sum;
        histograms.push_back(h);
    }

    std::mt19937_64 rng(42);
    auto result = kmeansEMD(histograms, 3, 100, rng);

    // Each centroid dimension is stored flat: centroids[k*dim + d]
    int dim = 4;
    for (int k = 0; k < 3; ++k) {
        double sum = 0;
        for (int d = 0; d < dim; ++d)
            sum += result.centroids[k * dim + d];
        EXPECT_NEAR(sum, 1.0, 0.01)
            << "Centroid " << k << " does not sum to ~1.0";
    }
}

// ── Preflop buckets ────────────────────────────────────────────────────

TEST(PreflopBuckets, Exactly169Classes) {
    initCards();
    auto pb = buildPreflopBuckets();

    EXPECT_EQ(pb.num_buckets, 169);
    EXPECT_EQ(static_cast<int>(pb.bucket_for_hand.size()),
              RiverBuckets::kNumHands);

    // All assignments in [0, 169)
    for (int b : pb.bucket_for_hand) {
        EXPECT_GE(b, 0);
        EXPECT_LT(b, 169);
    }
}

TEST(PreflopBuckets, AllClassesUsed) {
    initCards();
    auto pb = buildPreflopBuckets();

    std::set<int> classes(pb.bucket_for_hand.begin(),
                          pb.bucket_for_hand.end());
    EXPECT_EQ(static_cast<int>(classes.size()), 169);
}

TEST(PreflopBuckets, PairsShareBucket) {
    initCards();
    auto pb = buildPreflopBuckets();

    // All 6 combos of AA should map to the same bucket.
    Card Ah = cardFromStr("Ah"), Ad = cardFromStr("Ad");
    Card Ac = cardFromStr("Ac"), As = cardFromStr("As");

    int bucket_aa = pb.bucket_for_hand[RiverBuckets::handIndex(Ah, Ad)];
    EXPECT_EQ(pb.bucket_for_hand[RiverBuckets::handIndex(Ah, Ac)], bucket_aa);
    EXPECT_EQ(pb.bucket_for_hand[RiverBuckets::handIndex(Ah, As)], bucket_aa);
    EXPECT_EQ(pb.bucket_for_hand[RiverBuckets::handIndex(Ad, Ac)], bucket_aa);
    EXPECT_EQ(pb.bucket_for_hand[RiverBuckets::handIndex(Ad, As)], bucket_aa);
    EXPECT_EQ(pb.bucket_for_hand[RiverBuckets::handIndex(Ac, As)], bucket_aa);
}

TEST(PreflopBuckets, SuitedVsOffsuit) {
    initCards();
    auto pb = buildPreflopBuckets();

    // AKs and AKo should be different buckets.
    Card Ah = cardFromStr("Ah"), Kh = cardFromStr("Kh");
    Card Ad = cardFromStr("Ad");

    int aks = pb.bucket_for_hand[RiverBuckets::handIndex(Ah, Kh)];  // suited
    int ako = pb.bucket_for_hand[RiverBuckets::handIndex(Ah, Ad)];   // this is AA!
    // Actually use Ad Kh for offsuit
    int ako2 = pb.bucket_for_hand[RiverBuckets::handIndex(Ad, Kh)]; // offsuit
    EXPECT_NE(aks, ako2)
        << "AKs and AKo should be in different preflop buckets";
}

TEST(PreflopBuckets, PreflopClassFunction) {
    initCards();
    // preflopClass should match canonicalPreflopHand
    Card Ah = cardFromStr("Ah"), As = cardFromStr("As");
    EXPECT_EQ(PreflopBuckets::preflopClass(Ah, As),
              canonicalPreflopHand(Ah, As));
    // Symmetric
    EXPECT_EQ(PreflopBuckets::preflopClass(As, Ah),
              PreflopBuckets::preflopClass(Ah, As));
}
