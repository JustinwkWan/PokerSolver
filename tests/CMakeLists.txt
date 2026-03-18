include(GoogleTest)
enable_testing()

# ── test_cards ─────────────────────────────────────────────────────────────
add_executable(test_cards test_cards.cpp)
target_link_libraries(test_cards PRIVATE core GTest::gtest_main)
gtest_discover_tests(test_cards)

# ── test_hand_eval ─────────────────────────────────────────────────────────
add_executable(test_hand_eval test_hand_eval.cpp)
target_link_libraries(test_hand_eval PRIVATE core GTest::gtest_main)
gtest_discover_tests(test_hand_eval)

# ── test_game_state ────────────────────────────────────────────────────────
add_executable(test_game_state test_game_state.cpp)
target_link_libraries(test_game_state PRIVATE core GTest::gtest_main)
gtest_discover_tests(test_game_state)

# ── test_kuhn (Phase 2) ───────────────────────────────────────────────────
add_executable(test_kuhn test_kuhn.cpp)
target_link_libraries(test_kuhn PRIVATE core GTest::gtest_main)
gtest_discover_tests(test_kuhn)

# ── test_leduc (Phase 2) ──────────────────────────────────────────────────
add_executable(test_leduc test_leduc.cpp)
target_link_libraries(test_leduc PRIVATE core GTest::gtest_main)
gtest_discover_tests(test_leduc)

# ── test_equity (Phase 3) ────────────────────────────────────────────────
add_executable(test_equity test_equity.cpp)
target_link_libraries(test_equity PRIVATE abstraction core GTest::gtest_main)
gtest_discover_tests(test_equity)

# ── test_clustering (Phase 3) ────────────────────────────────────────────
add_executable(test_clustering test_clustering.cpp)
target_link_libraries(test_clustering PRIVATE abstraction core GTest::gtest_main)
gtest_discover_tests(test_clustering)

# ── test_action_abstraction (Phase 3/4) ────────────────────────────────
add_executable(test_action_abstraction test_action_abstraction.cpp)
target_link_libraries(test_action_abstraction PRIVATE abstraction core GTest::gtest_main)
gtest_discover_tests(test_action_abstraction)

# ── test_game_tree (Phase 4) ───────────────────────────────────────────
add_executable(test_game_tree test_game_tree.cpp)
target_link_libraries(test_game_tree PRIVATE tree abstraction core GTest::gtest_main)
gtest_discover_tests(test_game_tree)

# ── test_info_set (Phase 4) ────────────────────────────────────────────
add_executable(test_info_set test_info_set.cpp)
target_link_libraries(test_info_set PRIVATE tree abstraction core GTest::gtest_main)
gtest_discover_tests(test_info_set)
