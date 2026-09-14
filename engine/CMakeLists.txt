# Libreria de reglas pura: sin I/O, sin asignaciones en el hot path.
add_library(engine STATIC
  src/rules.cpp
)

target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine PRIVATE bsr_warnings)
target_compile_features(engine PUBLIC cxx_std_20)
