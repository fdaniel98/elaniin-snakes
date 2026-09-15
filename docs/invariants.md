---
title: Invariantes verificables del codigo
read_when: "antes de cambiar engine/, snake/ o cualquier estructura de datos del estado"
authority: canonical
last_verified: 2026-09-15
size_bytes: 5545
---

Un invariante sin verificacion mecanica es una nota, no un invariante. Cada fila dice
**como** se comprueba; las que no tienen comprobacion automatica estan marcadas como
NOTA y no cuentan.

## INV-01 Longitud y cuerpo {#inv-01}

`length` es siempre el numero de segmentos logicos del ring buffer, y `segment(i)` con
`i < length` nunca lee basura.

**Por que importa:** el resto del motor indexa por `length`; si divergen, el flood fill y
las colisiones leen casillas fantasma.
**Como se verifica:** `tests/test_rules.cpp` lo comprueba tras movimiento y crecimiento;
el fuzz de 10000 estados recorre todos los segmentos de cada estado generado.

## INV-02 Ocupacion consistente {#inv-02}

Toda casilla ocupada por el cuerpo de una serpiente viva esta marcada en `bodies`, y
viceversa.

**Por que importa:** el cerebro decide sobre el bitboard, no sobre los cuerpos.
**Como se verifica:** `refresh_occupancy()` lo reconstruye y el caso
"refresh_occupancy: el bitboard de ocupacion casa con los cuerpos vivos" lo compara.

## INV-03 Cero asignaciones en el hot path {#inv-03}

`apply()`, `legal_moves()` y el flood fill no piden memoria dinamica.

**Por que importa:** una asignacion en el hot path convierte la latencia en algo que
depende del allocator, y el presupuesto de 350 ms se mide, no se estima.
**Como se verifica:** por construccion (solo `std::array` y bitboards; el orden por
longitud usa insercion y no `std::stable_sort`, que puede pedir memoria temporal). La
comprobacion con allocator instrumentado es de la clase `perf` del loop: el umbral
`hot_path_allocs_max` de `config/loop.json` es 0.

## INV-04 Estado trivialmente copiable {#inv-04}

`GameState` se copia con `memcpy`: sin punteros a heap, sin `std::string`, sin `vector`.

**Por que importa:** la busqueda es copy-make; si el estado dejara de ser trivial, cada
nodo pagaria una asignacion.
**Como se verifica:** el benchmark `bm_copy_state` mide la copia; cualquier miembro no
trivial rompe la compilacion de `bench/bench_engine.cpp`.

## INV-05 El motor compila en los tres tamaños {#inv-05}

`Bitboard` y `GameState` estan instanciados explicitamente en 7x7, 11x11 y 19x19.

**Por que importa:** 11x11 es un *default*, no una constante esparcida por el codigo.
**Como se verifica:** `tests/test_bitboard.cpp` usa `TEMPLATE_TEST_CASE` sobre los tres
tamaños; `engine/src/rules.cpp` instancia las tres variantes.

## INV-06 Los desplazamientos no envuelven {#inv-06}

`east()`, `west()`, `north()` y `south()` no mueven bits de un borde al opuesto.

**Por que importa:** un bit que envuelve convierte una pared en un pasadizo y el flood
fill sobreestima el espacio.
**Como se verifica:** caso "los desplazamientos no envuelven por los bordes", en los tres
tamaños.

## INV-07 Placements sin desempate por indice {#inv-07}

Las serpientes eliminadas en el mismo turno reciben el mismo rango promediado.

**Por que importa:** el motor oficial no les asigna orden (ver docs/rules.md#r-12);
inventarlo sesgaria cualquier estadistica del Training Room.
**Como se verifica:** casos de empate de 2, 3 y 4 serpientes en `tests/test_rules.cpp`.

## INV-08 Aleatoriedad explicita y reproducible {#inv-08}

El motor no tiene estado global de RNG: toda aleatoriedad recibe un `Rng&`. Bajo
`engine/` y `arena/` estan prohibidos `std::uniform_int_distribution`, `std::shuffle`,
`std::sample` y `std::random_device`.

**Por que importa:** su algoritmo no esta especificado y difiere entre libstdc++ y
libc++, asi que la arena dejaria de ser reproducible entre maquinas.
**Como se verifica:** check 5 del gate (grep sobre `engine/` y `arena/`) y los vectores
de referencia de `tests/test_rng.cpp`.

## INV-09 La busqueda ignora el spawn de comida {#inv-09}

Ninguna funcion de decision modela la aparicion aleatoria de comida.

**Por que importa:** depende del `math/rand` de Go y de una semilla que no viaja en el
payload; modelarla seria inventar informacion.
**Como se verifica:** NOTA - hoy es una restriccion de diseño, no una comprobacion
automatica. Cuando exista la arena (fase 4) se verifica que el cerebro produce el mismo
movimiento con y sin comida futura inyectada.

## INV-10 Nunca un movimiento mortal evitable {#inv-10}

`decide()` jamas devuelve una direccion inmediatamente mortal si existe alguna que no lo
sea.

**Por que importa:** es la unica garantia que separa a v0 de una snake aleatoria.
**Como se verifica:** todos los fixtures mas el fuzz de 10000 estados aleatorios
comparan la respuesta contra `legal_moves`.

## INV-11 Nunca se excede el deadline {#inv-11}

`decide()` respeta el deadline incluso cuando es absurdamente corto.

**Por que importa:** el timeout del request incluye la latencia de red; pasarse es
perder la partida, no ir lento.
**Como se verifica:** caso con deadline artificial de 5 ms sobre cada fixture, y el fuzz,
que cuenta violaciones y exige 0.

## INV-12 El servidor nunca devuelve 5xx en /move {#inv-12}

Ante payload invalido, tablero sin instanciacion o excepcion, se responde 200 con el
movimiento del fail-safe.

**Por que importa:** un 5xx hace que el motor aplique su movimiento por defecto, que casi
nunca es el que nos conviene (ver docs/rules.md#r-03).
**Como se verifica:** check 8 del gate manda los fixtures y exige 200 con movimiento
legal; `tests/test_brain_v0.cpp` cubre los cuatro escalones del fail-safe.
