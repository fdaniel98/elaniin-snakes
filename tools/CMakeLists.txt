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
