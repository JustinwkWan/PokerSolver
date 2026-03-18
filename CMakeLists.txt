cmake_minimum_required(VERSION 3.20)
project(PokerSolver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -fsanitize=address,undefined")

# ── OMPEval (hand evaluator) ───────────────────────────────────────────────
# OMPEval uses a Makefile, not CMake.  We fetch it and build it ourselves.
include(FetchContent)

FetchContent_Declare(
  OMPEval
  GIT_REPOSITORY https://github.com/zekyll/OMPEval.git
  GIT_TAG        master
)
FetchContent_Populate(OMPEval)   # populates ompeval_SOURCE_DIR

# Only compile what Phase 1 needs. CombinedRange/EquityCalculator have a
# constexpr-max() incompatibility with libc++ 17+; they're not needed until
# Phase 3 (equity calculator is built from scratch there anyway).
add_library(omp_eval STATIC
  ${ompeval_SOURCE_DIR}/omp/HandEvaluator.cpp
  ${ompeval_SOURCE_DIR}/omp/CardRange.cpp
)
target_include_directories(omp_eval PUBLIC ${ompeval_SOURCE_DIR})
target_compile_options(omp_eval PRIVATE -O3)
set_target_properties(omp_eval PROPERTIES CXX_STANDARD 17)

# ── Google Test ────────────────────────────────────────────────────────────
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── cpp-httplib (header-only HTTP server) ─────────────────────────────────
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.15.3
)
FetchContent_MakeAvailable(httplib)

# ── nlohmann/json (header-only JSON library) ──────────────────────────────
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

enable_testing()

# ── Subdirectories ─────────────────────────────────────────────────────────
add_subdirectory(src/core)
add_subdirectory(src/abstraction)
add_subdirectory(src/tree)
add_subdirectory(src/solver)
add_subdirectory(src/subgame)
add_subdirectory(src/cli)
add_subdirectory(src/server)
add_subdirectory(tools)
add_subdirectory(tests)
