#include "tree/info_set.h"

#include <gtest/gtest.h>
#include <set>

// Shared tree + manager for most tests
static GameTree makeTree(int stack = 200) {
    ActionAbstraction aa;
    GameTree tree(stack, aa);
    tree.build();
    return tree;
}

static constexpr std::array<int, 4> kDefaultBuckets = {169, 500, 1000, 2000};
static constexpr std::array<int, 4> kSmallBuckets = {3, 5, 5, 10};

// ── Construction succeeds ──────────────────────────────────────────────

TEST(InfoSet, ConstructsSuccessfully) {
    auto tree = makeTree();
    InfoSetManager mgr(tree, kDefaultBuckets);

    EXPECT_GT(mgr.totalInfoSets(), 0u);
    EXPECT_GT(mgr.maxActions(), 0);
    EXPECT_EQ(mgr.numBaseInfoSets(), tree.numInfoSets());
}

// ── Total info sets scales with bucket counts ──────────────────────────

TEST(InfoSet, TotalScalesWithBuckets) {
    auto tree = makeTree();

    InfoSetManager small_mgr(tree, kSmallBuckets);
    InfoSetManager large_mgr(tree, kDefaultBuckets);

    EXPECT_GT(large_mgr.totalInfoSets(), small_mgr.totalInfoSets())
        << "More buckets should produce more total info sets";
}

// ── Info set IDs are unique for different (node, bucket) pairs ─────────

TEST(InfoSet, IdsUniqueSmallTree) {
    BetSizeConfig cfg;
    cfg.preflop = {1.0};
    cfg.flop = {1.0};
    cfg.turn = {1.0};
    cfg.river = {1.0};

    GameTree tree(20, ActionAbstraction(cfg));  // small tree
    tree.build();

    InfoSetManager mgr(tree, kSmallBuckets);

    std::set<uint64_t> seen;
    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type != NodeType::Action) continue;

        int street_buckets = mgr.bucketsForStreet(n.street);
        for (int b = 0; b < street_buckets; ++b) {
            uint64_t id = mgr.infoSetId(i, b);
            EXPECT_TRUE(seen.insert(id).second)
                << "Duplicate info set ID " << id
                << " at node " << i << " bucket " << b;
        }
    }
    EXPECT_EQ(static_cast<uint64_t>(seen.size()), mgr.totalInfoSets());
}

// ── Info set IDs are contiguous [0, total) ─────────────────────────────

TEST(InfoSet, IdsContiguous) {
    BetSizeConfig cfg;
    cfg.preflop = {1.0};
    cfg.flop = {1.0};
    cfg.turn = {1.0};
    cfg.river = {1.0};

    GameTree tree(20, ActionAbstraction(cfg));
    tree.build();

    InfoSetManager mgr(tree, kSmallBuckets);

    uint64_t max_id = 0;
    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type != NodeType::Action) continue;

        int street_buckets = mgr.bucketsForStreet(n.street);
        for (int b = 0; b < street_buckets; ++b) {
            uint64_t id = mgr.infoSetId(i, b);
            EXPECT_LT(id, mgr.totalInfoSets());
            if (id > max_id) max_id = id;
        }
    }
    EXPECT_EQ(max_id, mgr.totalInfoSets() - 1);
}

// ── Per-base metadata matches tree ─────────────────────────────────────

TEST(InfoSet, BaseMetadataMatchesTree) {
    auto tree = makeTree();
    InfoSetManager mgr(tree, kDefaultBuckets);

    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type != NodeType::Action) continue;

        uint32_t base = n.info_set_idx;
        EXPECT_EQ(mgr.numActionsForBase(base), static_cast<int>(n.num_actions));
        EXPECT_EQ(mgr.playerForBase(base), static_cast<int>(n.player));
        EXPECT_EQ(mgr.streetForBase(base), static_cast<int>(n.street));
    }
}

// ── numActions matches tree node ───────────────────────────────────────

TEST(InfoSet, NumActionsMatches) {
    auto tree = makeTree();
    InfoSetManager mgr(tree, kDefaultBuckets);

    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type != NodeType::Action) continue;
        EXPECT_EQ(mgr.numActions(i), static_cast<int>(n.num_actions));
    }
}

// ── bucketsForStreet returns correct values ─────────────────────────────

TEST(InfoSet, BucketsForStreet) {
    auto tree = makeTree();
    InfoSetManager mgr(tree, kDefaultBuckets);

    EXPECT_EQ(mgr.bucketsForStreet(0), 169);
    EXPECT_EQ(mgr.bucketsForStreet(1), 500);
    EXPECT_EQ(mgr.bucketsForStreet(2), 1000);
    EXPECT_EQ(mgr.bucketsForStreet(3), 2000);
}

// ── Both players appear in base info sets ──────────────────────────────

TEST(InfoSet, BothPlayersPresent) {
    auto tree = makeTree();
    InfoSetManager mgr(tree, kDefaultBuckets);

    bool player_seen[2] = {};
    for (uint32_t i = 0; i < mgr.numBaseInfoSets(); ++i) {
        player_seen[mgr.playerForBase(i)] = true;
    }
    EXPECT_TRUE(player_seen[0]);
    EXPECT_TRUE(player_seen[1]);
}

// ── All 4 streets appear in base info sets ─────────────────────────────

TEST(InfoSet, AllStreetsPresent) {
    auto tree = makeTree();
    InfoSetManager mgr(tree, kDefaultBuckets);

    bool street_seen[4] = {};
    for (uint32_t i = 0; i < mgr.numBaseInfoSets(); ++i) {
        street_seen[mgr.streetForBase(i)] = true;
    }
    for (int s = 0; s < 4; ++s)
        EXPECT_TRUE(street_seen[s]) << "Missing street " << s;
}

// ── Same bucket on different nodes → different IDs ─────────────────────

TEST(InfoSet, SameBucketDifferentNodes) {
    auto tree = makeTree();
    InfoSetManager mgr(tree, kSmallBuckets);

    // Find two different action nodes on the same street
    uint32_t node_a = UINT32_MAX, node_b = UINT32_MAX;
    for (uint32_t i = 0; i < tree.numNodes(); ++i) {
        const auto& n = tree.node(i);
        if (n.type != NodeType::Action) continue;
        if (node_a == UINT32_MAX) {
            node_a = i;
        } else if (n.info_set_idx != tree.node(node_a).info_set_idx) {
            node_b = i;
            break;
        }
    }
    ASSERT_NE(node_a, UINT32_MAX);
    ASSERT_NE(node_b, UINT32_MAX);

    uint64_t id_a = mgr.infoSetId(node_a, 0);
    uint64_t id_b = mgr.infoSetId(node_b, 0);
    EXPECT_NE(id_a, id_b);
}
