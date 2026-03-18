add_library(subgame STATIC
    action_translation.cpp
    subgame_solver.cpp
)

target_include_directories(subgame PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(subgame PUBLIC solver tree abstraction core)
