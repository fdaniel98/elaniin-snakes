find_package(benchmark REQUIRED)

add_executable(bench_engine bench_engine.cpp)
target_link_libraries(bench_engine PRIVATE engine snake_brain bsr_warnings benchmark::benchmark)
