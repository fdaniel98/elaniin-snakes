# Un unico binario de tests: mas rapido de enlazar y `ctest` descubre cada TEST_CASE.
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(unit_tests
  test_bitboard.cpp
  test_rng.cpp
  test_rules.cpp
  test_ruleset_parse.cpp
  test_brain_v0.cpp
  test_differential.cpp
  test_voronoi.cpp
)

target_link_libraries(unit_tests PRIVATE engine snake_brain bsr_warnings Catch2::Catch2WithMain)
target_include_directories(unit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(unit_tests PRIVATE
  BSR_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  BSR_CONFIG_DIR="${CMAKE_SOURCE_DIR}/snake/config"
  BSR_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus"
)

# Los tests [.pending] quedan registrados y visibles, pero no corren por defecto.
catch_discover_tests(unit_tests)
