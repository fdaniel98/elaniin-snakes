# Self-play in-process. Sin HTTP, sin JSON, sin reloj: presupuesto por nodos.
add_library(arena STATIC
  src/arena.cpp
)

target_include_directories(arena PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(arena PUBLIC engine snake_brain)
target_link_libraries(arena PRIVATE bsr_warnings)
target_compile_features(arena PUBLIC cxx_std_20)
