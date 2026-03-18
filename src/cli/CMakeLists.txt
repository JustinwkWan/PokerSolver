# ── solver_api library ─────────────────────────────────────────────────────
add_library(solver_api STATIC solver_api.cpp)
target_include_directories(solver_api PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(solver_api PUBLIC subgame solver tree abstraction core)

# ── poker-solver CLI ──────────────────────────────────────────────────────
add_executable(poker-solver main.cpp)
target_include_directories(poker-solver PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(poker-solver PRIVATE solver_api server)
