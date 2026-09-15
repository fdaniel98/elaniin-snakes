# Battlesnake Royale (C++20) + Training Room

Snake competitiva para **Royale** (11x11, 4 serpientes, timeout 500 ms) en C++20, con
foco en rendimiento medible, mas un Training Room local para entrenarla contra snakes
publicas. El motor es una libreria pura; el cerebro y el servidor viven aparte.

## Orden de lectura obligatorio

1. `STATE.md` - fase, gate, loop vigente y siguiente accion concreta.
2. `docs/INDEX.md` - que leer y, sobre todo, que **no** leer.
3. El context pack de tu tarea, en `docs/context-packs/`.

No explores a ciegas: si no sabes que pack usar, empieza por `docs/INDEX.md`.

## Reglas de oro

1. **No inventes reglas del juego.** La fuente es el Go de `BattlesnakeOfficial/rules` en
   el SHA fijado en `docs/SOURCES.md`. Si tras leer el codigo sigue ambiguo, **pregunta**.
2. **No hay optimizacion sin medicion.** Benchmark antes y despues, delta absoluto y
   relativo, en `docs/performance.md`.
3. **Ningun cambio de estrategia entra por intuicion:** tiene que ganar su A/B.
4. **El gate es el arbitro y solo crece.** Si el gate pasa y algo esta mal, el bug esta en
   el gate: añade el check. Debilitarlo exige aprobacion humana y un ADR.
5. **Nunca hardcodees parametros del ruleset:** se leen del request por su ruta JSON
   exacta; los defaults son *fallbacks* y usarlos emite `WARN`.
6. **Nunca un movimiento ilegal ni un deadline excedido.** Ante lo que sea, fail-safe.
7. **Commits pequeños.** Toda decision con alternativa descartada va a `docs/decisions/`.
8. Toolchain en WSL2 (Ubuntu 24.04); el repo se edita desde Windows.
9. **Nada es canonico por estar escrito aqui:** si la fuente contradice a la doc, gana la
   fuente; se corrige el codigo, la doc y `docs/SOURCES.md`.
10. `scripts/gate.sh`, `config/loop.json` y `.claude/settings.json` no se tocan sin
    aprobacion humana explicita y un ADR.
11. **Ningun entregable se cierra sin su loop:** 3 iteraciones de clases distintas con
    ledger valido. Gate verde es necesario, no suficiente.

## Comandos canonicos

Todo corre dentro de WSL2. Desde Windows: `wsl -d Ubuntu-24.04 -e bash -lc '<comando>'`.

```bash
./scripts/bootstrap.sh                  # verifica el toolchain (--install lo instala)
cmake --preset release && cmake --build --preset release
ctest --preset release
./scripts/gate.sh                       # veredicto de "hecho"; --fast NO cierra fase
./scripts/gate-selftest.sh              # prueba que el gate sirve
./scripts/loop.sh <n> <slug> [clase]    # una iteracion del loop
./scripts/bench.sh                      # linea base publicable (ISA de deploy)
PORT=8080 ./build/release/bin/battlesnake-server
```

## Anti-patrones

Los que enuncian una **regla del juego** no la afirman aqui: enlazan a su anchor.

- Cola: ver docs/rules.md#r-04 antes de asumir que la casilla de cola esta libre.
- Ejes: ver docs/rules.md#r-01 antes de asumir el origen de coordenadas.
- Hazards: ver docs/rules.md#r-09 antes de asumir el lado del proximo shrink.
- Orden del turno: ver docs/rules.md#r-02 antes de asumir que el turno empieza
  moviendo.
- Placements: ver docs/rules.md#r-12 antes de desempatar muertes simultaneas.

Los de **ingenieria** si se enuncian, porque no dependen de la fuente Go:

- Battlesnake es de **movimientos simultaneos**: no razones como si fuera por turnos.
- `-march=native` esta prohibido en el preset `deploy`.
- Nada de `new` ni `malloc` dentro de `engine/` en el hot path.
- El timeout del request **incluye** la latencia de red.
- Ningun entregable se cierra sin su loop.
- `std::shuffle`, `std::uniform_int_distribution`, `std::sample` y `std::random_device`
  estan prohibidos bajo `engine/` y `arena/`: su algoritmo no esta especificado.

## Donde escribir que

Un hecho, un lugar. La tabla de quien es dueño de que esta en un unico sitio:
ver docs/INDEX.md#i-04.
