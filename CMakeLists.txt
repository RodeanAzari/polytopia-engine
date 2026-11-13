cmake_minimum_required(VERSION 3.24)
project(polytopia_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Werror)

option(POLY_SANITIZE "Build with ASan/UBSan" OFF)
if(POLY_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(poly_sim STATIC sim/src/state.cpp sim/src/level.cpp sim/src/dump.cpp sim/src/rules.cpp sim/src/move.cpp sim/src/network.cpp sim/src/observation.cpp sim/src/action_encoding.cpp sim/src/features.cpp)
target_include_directories(poly_sim PUBLIC sim/include)

add_library(poly_search STATIC search/determinize.cpp search/diff_heuristic.cpp search/macro.cpp search/match.cpp search/mc.cpp search/mcts.cpp search/osla.cpp search/puct.cpp search/rhea.cpp search/root_groups.cpp search/rule_based.cpp)
target_include_directories(poly_search PUBLIC .)
target_link_libraries(poly_search PUBLIC poly_sim)

add_library(poly_selfplay STATIC selfplay/record.cpp selfplay/recorder.cpp)
target_include_directories(poly_selfplay PUBLIC .)
target_link_libraries(poly_selfplay PUBLIC poly_search)

add_executable(selfplay tools/selfplay.cpp)
target_link_libraries(selfplay PRIVATE poly_selfplay)

add_executable(oracle_diff tools/oracle_diff.cpp)
target_link_libraries(oracle_diff PRIVATE poly_search)

add_executable(bench tools/bench.cpp)
target_link_libraries(bench PRIVATE poly_sim)

add_executable(tournament tools/tournament.cpp)
target_link_libraries(tournament PRIVATE poly_search)

add_executable(record tools/record.cpp)
target_link_libraries(record PRIVATE poly_search)

include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(sim_tests tests/sim/test_rng.cpp tests/sim/test_inline_vector.cpp tests/sim/test_level.cpp tests/sim/test_actions.cpp tests/sim/test_fog.cpp tests/sim/test_action_encoding.cpp tests/sim/test_agent_advance.cpp tests/sim/test_features.cpp)
target_link_libraries(sim_tests PRIVATE poly_sim GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(sim_tests)

add_executable(search_tests tests/search/test_determinize.cpp tests/search/test_diff_heuristic.cpp tests/search/test_macro.cpp tests/search/test_mc.cpp tests/search/test_mcts.cpp tests/search/test_osla.cpp tests/search/test_puct.cpp tests/search/test_rhea.cpp tests/search/test_rule_based.cpp tests/search/test_match.cpp tests/search/test_puct_net.cpp)
target_link_libraries(search_tests PRIVATE poly_search GTest::gtest_main)
gtest_discover_tests(search_tests)

add_executable(selfplay_tests tests/selfplay/test_record.cpp)
target_link_libraries(selfplay_tests PRIVATE poly_selfplay GTest::gtest_main)
gtest_discover_tests(selfplay_tests)
