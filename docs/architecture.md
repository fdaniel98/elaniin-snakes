---
title: Arquitectura y dependencias entre modulos
read_when: "antes de mover codigo entre modulos o de añadir una dependencia"
authority: canonical
last_verified: 2026-09-15
size_bytes: 2610
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
