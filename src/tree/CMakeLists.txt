add_library(tree STATIC
    game_tree.cpp
    info_set.cpp
)

target_include_directories(tree PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tree PUBLIC abstraction core)
