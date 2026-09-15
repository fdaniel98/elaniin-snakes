# engine/ - invariantes locales

Libreria de reglas pura. Contrato del modulo: **sin I/O, sin JSON, sin red, sin estado
global y sin asignaciones en el hot path**. No incluyas nada de `snake/` aqui.

## Lo que no se negocia

- `GameState` es trivialmente copiable: ni punteros a heap, ni `string`, ni `vector`.
  La busqueda copia estados con `memcpy` (ver docs/invariants.md#inv-04).
- Cero asignaciones dinamicas en funciones que corran por nodo: ver docs/invariants.md#inv-03
  (ahi esta la lista completa y el porque de `std::stable_sort`).
- Toda aleatoriedad recibe un `Rng&` explicito, y los generadores de la STL estan prohibidos
  aqui: ver docs/invariants.md#inv-08.
- `apply()` **no filtra** direcciones: acepta la inmediatamente mortal y reproduce el
  movimiento por defecto del arbitro. `legal_moves()` es una ayuda para el cerebro, no una
  regla (ver docs/rules.md#r-03).
- El orden de fases del turno es el del arbitro y no se reordena por comodidad
  (ver docs/rules.md#r-02).
- Los empates no se desempatan por indice ni por asiento (ver docs/rules.md#r-12).

## Antes de tocar rules.cpp

Lee `docs/context-packs/implementar-regla.md`. Si la regla que vas a implementar no esta
en `docs/rules.md` con su cita `archivo.go:linea`, para y verificala primero: implementar
contra `speculative` esta prohibido.

## Instanciaciones

`Bitboard` y `GameState` se instancian explicitamente en 7x7, 11x11 y 19x19. Si añades un
tamaño, añadelo tambien en `engine/src/rules.cpp` y en `tests/test_bitboard.cpp`, o el
servidor caera al fail-safe con `WARN` para ese tablero.
