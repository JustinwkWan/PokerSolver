# ── HTTP server library ───────────────────────────────────────────────────
add_library(server STATIC http_server.cpp)
target_include_directories(server PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(server PUBLIC solver_api httplib nlohmann_json::nlohmann_json)
