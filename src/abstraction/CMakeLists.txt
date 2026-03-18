add_library(abstraction STATIC
    equity_calculator.cpp
    hand_clustering.cpp
    action_abstraction.cpp
    suit_isomorphism.cpp
)

target_include_directories(abstraction PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(abstraction PUBLIC core)
