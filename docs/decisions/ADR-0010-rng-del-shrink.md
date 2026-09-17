---
title: "ADR-0010: el shrink de royale usa el Rng propio, no el math/rand de Go"
read_when: "antes de tocar royale_hazards, de modelar hazards en la arena, o de comparar una partida propia con una oficial"
authority: canonical
last_verified: 2026-09-17
size_bytes: 3084
---


## D-0090 Contexto {#d-0090}

`royale_hazards()` lanzaba `logic_error` desde la fase 0 y es trabajo de la fase 1. La
regla que tiene que implementar ya esta verificada contra la fuente y documentada con sus
citas: ver docs/rules.md#r-09.

Lo que no esta decidido es **de donde sale el lado de cada shrink**. El motor oficial lo
saca de `rand.New(rand.NewSource(seed))`, que es el generador de retardo de Fibonacci de
`math/rand` (`rand.go:31-53`, `settings.go:41-52`). Reproducirlo desde C++ es posible
--el algoritmo esta congelado por la promesa de compatibilidad de Go-- pero exige copiar
su tabla de 607 constantes, que no se puede regenerar: el programa que la produjo
necesita 7.8e12 iteraciones (`gen_cooked.go` de la biblioteca estandar).

## D-0091 Decision {#d-0091}

`royale_hazards()` usa el **PCG64 propio** de `engine/include/engine/rng.hpp`, ya
existente y con sus vectores de prueba. La secuencia de lados es nuestra; la **forma** del
schedule es la del motor oficial, con cita.

Decidido por el humano el 2026-09-16: preferencia explicita por codigo propio, no solo
coste.

La firma documentada en `rules.hpp` no cambia: `royale_hazards(seed, turn,
shrink_every_n_turns)` construye un `Rng` local a partir de la semilla, igual que
`settings.GetRand(0)` en el Go. Es una desviacion de la letra de INV-08, que pide un
`Rng&` explicito (ver docs/invariants.md#inv-08): se registra aqui en vez de dejarla
implicita, y no rompe su motivo -no hay estado global y el resultado depende solo de los
argumentos-.

## D-0092 Alternativas descartadas {#d-0092}

| Alternativa | Por que no |
|---|---|
| Portar el `rngSource` de Go a C++ | Mete en `engine/` algoritmo y tabla de constantes de terceros. El proyecto prefiere codigo propio |
| Leer el schedule del log en la arena | El JSONL no trae la semilla ni el rectangulo, solo las casillas; y la arena tiene que generar partidas nuevas, no releer viejas |
| Dejar `royale_hazards` sin implementar hasta la fase 4 | La arena de la fase 4 no puede diseñarse sin saber que modelo de hazard va a consumir |

## D-0093 Consecuencias {#d-0093}

- Una partida de la arena **nunca reproducira** una partida oficial casilla por casilla:
  coinciden el rectangulo y su cadencia, no que borde toca cada vez. El A/B de la fase 4
  no se ve afectado -compara dos configuraciones nuestras entre si, con la misma semilla-,
  pero cualquier comparacion contra un log oficial si.
- El test diferencial **inyecta** comida y hazards del log, como ya preveia
  docs/rules-parametros.md#r-99. Esta decision no lo cambia: lo confirma.
- Como `royale_hazards()` se queda sin contraste externo, el corpus del diferencial
  comprueba en su lugar la unica propiedad del shrink que si es derivable de un log sin
  reproducir el RNG, la que enuncia ver docs/rules.md#r-09.
