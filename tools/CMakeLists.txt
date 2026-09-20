# Herramientas de analisis. No entran en el deploy: viven aqui para que el Training Room
# pueda pedirle al MOTOR VERIFICADO lo que el JSONL no trae, en vez de reimplementar las
# reglas en Python y tener dos versiones que se separan.
add_executable(causas causas.cpp)
# `snake_brain` ya arrastra nlohmann/json con la misma resolucion (paquete del sistema o
# la copia de third_party), asi que no se duplica la busqueda aqui.
target_link_libraries(causas PRIVATE engine snake_brain bsr_warnings)
target_include_directories(causas PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# Sonda de cuellos: mide cuantas veces se dispara la penalizacion de salas con una sola
# puerta. Se construye con el resto para que no se pudra. ver docs/strategy.md#s-cuellos-r
add_executable(sonda_cuellos sonda_cuellos.cpp)
target_link_libraries(sonda_cuellos PRIVATE engine snake_brain bsr_warnings)

add_executable(sonda_busqueda sonda_busqueda.cpp)
# `snake_brain` ya expone nlohmann como PUBLIC, asi que no se busca otra vez aqui.
target_link_libraries(sonda_busqueda PRIVATE engine snake_brain bsr_warnings)
target_compile_definitions(sonda_busqueda PRIVATE
  BSR_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# Cuanto tiempo deja sin usar el tope de profundidad segun cuantas serpientes viven.
add_executable(sonda_tope sonda_tope.cpp)
target_link_libraries(sonda_tope PRIVATE engine snake_brain bsr_warnings)

add_executable(sonda_overshoot sonda_overshoot.cpp)
target_link_libraries(sonda_overshoot PRIVATE engine snake_brain bsr_warnings)

add_executable(sonda_arena sonda_arena.cpp)
target_link_libraries(sonda_arena PRIVATE arena engine snake_brain bsr_warnings)

find_package(Threads REQUIRED)
add_executable(arena_torneo arena_torneo.cpp)
target_link_libraries(arena_torneo PRIVATE arena engine snake_brain bsr_warnings Threads::Threads)
