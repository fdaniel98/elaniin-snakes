---
name: cpp-hotpath
description: Reglas de C++ de alto rendimiento para el motor. Usar al editar cualquier archivo de engine/src o engine/include, al revisar un diff que toque apply, legal_moves o el flood fill, al interpretar un perfil de perf, y al decidir si un cambio merece la pena.
---

# C++ en el hot path

El hot path es todo lo que corre una vez por nodo de busqueda: `apply`, `legal_moves`, el
flood fill y la copia de estado. Fuera de ahi, prioriza claridad.

## Prohibido

- Asignar memoria: nada de `new`, `malloc`, `std::vector`, `std::string`, `std::function`.
  Ojo con `std::stable_sort`, que puede pedir memoria temporal (ver docs/invariants.md#inv-03).
- Estado global mutable, incluido cualquier RNG (ver docs/invariants.md#inv-08).
- Excepciones como control de flujo en funciones por nodo.
- Virtualidad en tipos que se instancian por nodo.

## Preferido

- Datos pequeños y contiguos: `std::array`, indices en vez de punteros, tipos compactos
  (`uint8_t` para salud, `uint16_t` para casilla).
- Bitboard antes que bucles por casilla: `expand()` hace un paso de flood fill sobre todo
  el tablero en unas pocas instrucciones.
- `std::popcount` y `std::countr_zero` en vez de bucles de bits; por eso el preset
  `deploy` fija `-march=x86-64-v2` (ver docs/decisions/ADR-0004-deploy.md).
- Parametros de tablero en tiempo de compilacion: un tamaño en runtime convierte cada
  indice en una multiplicacion por variable.
- Salir temprano de bucles con `break`, y comprobar el deadline **entre** candidatos, no
  dentro de la parte barata.

## Antes de optimizar

1. Mide con `./scripts/bench.sh` (preset `bench-deployisa`). Sin numero previo no hay
   optimizacion.
2. Perfila con el preset `profile` antes de reescribir nada.
3. Un delta por debajo de `regression_pct` de `config/loop.json` es RUIDO.
4. Todo numero con `-march=native` es `local-only` y no sirve de linea base.

## Checklist de revision

- [ ] Ninguna asignacion nueva en el hot path.
- [ ] El estado sigue siendo trivialmente copiable (ver docs/invariants.md#inv-04).
- [ ] Sin avisos nuevos de `-Wconversion`; los `static_cast` dicen que se convierte y por que.
- [ ] Sin cambios de comportamiento: los tests de reglas siguen verdes sin tocarlos.
- [ ] Benchmark antes y despues en el mensaje del commit.
- [ ] Si el cambio toca el deadline, hay un caso con deadline artificial de 5 ms.
