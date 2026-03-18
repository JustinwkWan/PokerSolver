add_library(solver STATIC
    regret_store.cpp
    cfr_solver.cpp
    exploitability.cpp
)

target_include_directories(solver PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(solver PUBLIC tree abstraction core)
