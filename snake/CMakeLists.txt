# Cerebro + servidor. El cerebro es una libreria para que la arena y los tests usen
# exactamente el mismo `decide` que el servidor.
add_library(snake_brain STATIC
  src/brain_v0.cpp
  src/config_loader.cpp
)

find_package(nlohmann_json 3 QUIET)

target_include_directories(snake_brain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(snake_brain PUBLIC engine)
target_link_libraries(snake_brain PRIVATE bsr_warnings)
if(nlohmann_json_FOUND)
  target_link_libraries(snake_brain PUBLIC nlohmann_json::nlohmann_json)
endif()

add_executable(battlesnake-server src/server.cpp)
target_link_libraries(battlesnake-server PRIVATE snake_brain bsr_warnings)

# cpp-httplib vendorizado como cabecera unica (MIT, third_party/cpp-httplib):
# el paquete de Debian es una libreria compartida, y arrastrarla al runtime
# distroless multiplicaria las piezas moviles del deploy. Entra como SYSTEM para que
# sus avisos no rompan -Werror. ver docs/decisions/ADR-0003-dependencias.md
target_include_directories(battlesnake-server SYSTEM PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib)

find_package(Threads REQUIRED)
target_link_libraries(battlesnake-server PRIVATE Threads::Threads)
