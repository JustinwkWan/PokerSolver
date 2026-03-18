add_library(core STATIC
    cards.cpp
    hand_evaluator.cpp
    game_state.cpp
)

target_include_directories(core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(core PUBLIC omp_eval)
