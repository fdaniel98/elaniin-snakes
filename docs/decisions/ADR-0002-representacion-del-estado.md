---
title: "ADR-0002: bitboard parametrizado, ring buffer y copy-make"
read_when: "antes de cambiar la representacion del estado o la firma de apply()"
authority: canonical
last_verified: 2026-09-15
size_bytes: 1892
---


## D-0010 Contexto {#d-0010}

El motor tiene que simular muchos turnos por movimiento dentro de un presupuesto de
350 ms, y la busqueda futura (fases 2 a 4) copiara estados sin parar.

## D-0011 Decision {#d-0011}

1. **Bitboard parametrizado en tiempo de compilacion**: `Bitboard<W,H>` con
   `ceil(W*H/64)` palabras, instanciado en 7x7, 11x11 y 19x19. `Board11` es el alias por
   defecto y ocupa dos `uint64_t`.
2. **Cuerpos en ring buffer de tamaño fijo** dentro del propio estado: avanzar la cabeza
   es O(1) y no mueve memoria.
3. **Copy-make**: el llamante copia el estado (trivialmente copiable) y `apply` muta la
   copia. No hay `undo`.

## D-0012 Alternativas descartadas {#d-0012}

| Alternativa | Por que no |
|---|---|
| Tablero como `std::vector<Cell>` | Asigna en el hot path y la copia deja de ser `memcpy` |
| `std::deque` para el cuerpo | Misma razon, y ademas el recorrido pierde localidad |
| Make/unmake con pila de deshacer | Menos memoria por nodo, pero el estado aqui cabe en pocos cientos de bytes y `undo` es una fuente clasica de bugs de estado inconsistente |
| Tamaño de tablero en runtime | Convierte cada indice en una multiplicacion por variable y bloquea el desenrollado; el despacho por tamaño se hace una vez, en el servidor |

## D-0013 Consecuencias {#d-0013}

- Un tablero sin instanciacion explicita no se puede jugar: el servidor cae al fail-safe
  con `WARN` (ver docs/rules.md#r-03).
- El maximo de serpientes tambien es constante de compilacion (4 por defecto).
- `std::stable_sort` queda prohibido en el hot path porque puede pedir memoria temporal;
  el orden por longitud usa insercion (ver docs/invariants.md#inv-03).
