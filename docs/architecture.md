---
title: Arquitectura y dependencias entre modulos
read_when: "antes de mover codigo entre modulos o de añadir una dependencia"
authority: canonical
last_verified: 2026-09-17
size_bytes: 4108
---

## A-01 Grafo de dependencias {#a-01}

Las flechas son "depende de". No hay ciclos, y la direccion no se invierte sin ADR.

```
                     engine/                 C++20 puro: sin I/O, sin JSON, sin red
                 reglas + estado
                        ^
          +-------------+--------------+
          |                            |
   snake/ (cerebro)            arena/ [fase 4]
   decide() + eval  <--------  self-play por nodos
          ^                            ^
   snake/src/server            training-room/ [fase 3]
   HTTP + JSON                         ^
                                 zoo/ [fase 3]
```

## A-02 Una sola interfaz de cerebro {#a-02}

`Move decide(const GameState&, Deadline, const Params&)` la usan **identicamente** el
servidor y la arena. Si la arena midiera algo distinto de lo que juega el servidor,
ningun A/B significaria nada.

## A-03 Donde vive el JSON {#a-03}

`engine/` no depende de ninguna libreria de JSON. La **forma** del payload la conoce un solo
archivo, `snake/src/config_loader.cpp` (ver docs/rules-parametros.md#r-20);
`snake/src/server.cpp` tambien usa nlohmann/json, pero solo para parsear el cuerpo crudo y
construir la respuesta, sin interpretar campos del juego.
`engine/include/engine/ruleset.hpp` tiene el struct de parametros, sus fallbacks, el enum de
variante y `is_supported`.

Consecuencia practica: cambiar nlohmann/json por simdjson toca esos dos archivos y ninguno
mas, y solo si el perfil demuestra que el parseo es un cuello de botella real.

## A-04 Terceros {#a-04}

| Dependencia | Como entra | Por que |
|---|---|---|
| Catch2 3.4 | paquete del sistema | tests |
| Google Benchmark 1.8 | paquete del sistema | benchmarks |
| nlohmann/json 3.11 | paquete del sistema | parseo del request |
| cpp-httplib 0.18.7 | vendorizado en `third_party/` | el paquete de Debian es una libreria compartida y el runtime distroless no la tendria (ver docs/decisions/ADR-0003-dependencias.md) |

## A-05 Que NO hace el motor {#a-05}

- No genera hazards de royale en produccion: la semilla no viaja en el payload (ver
  docs/rules.md#r-09).
- No modela el spawn de comida (ver docs/invariants.md#inv-09).
- No mantiene estado global: ni RNG, ni caches, ni singletons.
- No filtra movimientos en `apply()`: acepta el mortal, porque el test diferencial de la
  fase 1 necesita reproducir los logs del arbitro (ver docs/rules.md#r-03).

## A-06 Desviaciones del arbol de archivos especificado {#a-06}

- `tests/test_rng.cpp`: los vectores de referencia del RNG necesitaban su propio archivo.
- `snake/include/snake/config_loader.hpp`: la frontera de JSON necesita cabecera propia
  para que los tests la usen.
- `snake/include/snake/eval/floodfill.hpp` no es un stub: `brain_v0` necesita flood fill.
  `voronoi.hpp` y `features.hpp` si lo son.
- `scripts/smoke.py` y `scripts/mutants.sh`: el check 8 y la prueba de mutantes necesitan
  una implementacion independiente de la del motor.
- `third_party/cpp-httplib/`: ver docs/decisions/ADR-0003-dependencias.md.
- `docs/rules-parametros.md`: `docs/rules.md` se partio en dos para no pasarse del
  presupuesto por tarea (ver docs/INDEX.md#i-02).
- `engine/src/royale_map.cpp`: los hazards del mapa royale van aparte del pipeline del
  turno, como en la fuente, que separa `standard.go` de `maps/royale.go`.
- `tests/replay/`: el test diferencial necesita tres piezas que no son tests -proxy
  grabador, snake de caos y la cabecera del replay- y ninguna cabe en `arena/`, que es
  in-process y de la fase 4.
- `tests/corpus/`: dato de prueba versionado, generado y nunca editado a mano
  (ver docs/decisions/ADR-0011-corpus-del-diferencial.md#d-0101).
- `scripts/gen-replays.sh` y `scripts/build-referee-mirrored.sh`: generar el corpus exige
  orquestar al arbitro oficial, y compilarlo donde el proxy de modulos de Go esta
  bloqueado exige su propio script (ver docs/SOURCES.md#s-02).
