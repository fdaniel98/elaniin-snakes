---
title: "ADR-0011: corpus del test diferencial, commiteado pequeño y regenerable grande"
read_when: "antes de tocar tests/corpus, scripts/gen-replays.sh o el test diferencial"
authority: canonical
last_verified: 2026-09-17
size_bytes: 3495
---


## D-0100 Contexto {#d-0100}

La DoD de la fase 1 pide >=500 partidas reproducidas sin divergencia. Una partida son dos
ficheros: el JSONL del arbitro y el log del proxy grabador, porque el JSONL **no trae los
movimientos** y la serpiente que muere desaparece del turno siguiente
(ver docs/rules.md#r-12), justo el caso que hay que verificar.

500 partidas ocupan del orden de decenas de MB en el arbol de trabajo. Correrlas todas en
cada gate multiplicaria por varios los cuatro minutos que cuesta hoy.

## D-0101 Decision {#d-0101}

Dos corpus, con papeles distintos:

| | Commiteado | Regenerado |
|---|---|---|
| Que es | `tests/corpus/`, decenas de partidas | >=500 partidas |
| Quien lo corre | `ctest`, en cada gate | `scripts/gen-replays.sh --games 500` a mano |
| Para que | **regresion**: que una divergencia arreglada no vuelva | **DoD**: la evidencia de la fase |

Las semillas y los parametros de cada partida quedan en `index.json`, asi que el corpus
grande es reproducible bit a bit desde el generador y no hace falta guardarlo.

Decidido por el humano el 2026-09-16.

## D-0102 Como se elige el corpus commiteado {#d-0102}

No por muestreo: **por cobertura**. La primera version del corpus -32 partidas generadas
de una matriz de escenarios razonable- reprodujo 680 turnos sin una sola divergencia y
parecia una buena noticia. Al contar las causas de muerte resulto que solo cubria dos de
las seis: pared y autocolision. Ni hambre, ni hazard, ni colision con rival, ni cabeza a
cabeza. Un mutante que rompia el empate de longitudes pasaba el replay entero sin que
nadie se enterara.

Por eso el replay cuenta cobertura y el test la exige: cada causa de `Elimination` tiene
que aparecer al menos una vez, mas un empate cabeza a cabeza y una muerte simultanea. Y
por eso la matriz de escenarios del generador tiene entradas cuyo unico proposito es
producir una causa concreta -sin comida para que las longitudes no cambien y todo choque
frontal sea empate, hazard de 100 para que matar sea inmediato, comida a chorro para que
los cuerpos crezcan y haya colisiones contra rival-.

## D-0103 Alternativas descartadas {#d-0103}

| Alternativa | Por que no |
|---|---|
| Commitear las 500 partidas | Decenas de MB en el arbol y un gate varias veces mas lento, para cubrir lo mismo que cubren unas decenas bien elegidas |
| No commitear ninguna y regenerar siempre | El gate pierde la regresion: una divergencia reintroducida en la fase 5 no la ve nadie hasta que alguien se acuerde de correr el generador |
| Guardar el corpus comprimido | Obliga a meter zlib en los tests; git ya comprime los objetos, asi que el coste real en el repositorio es el mismo |
| Recortar campos del JSONL para que ocupe menos | Deja de ser el log del arbitro tal cual, que es justo lo que le da valor como fuente |

## D-0104 Consecuencias {#d-0104}

- El test diferencial corre en el check 3 del gate, con el corpus commiteado. El gate
  comprueba **mas** que antes (regla de oro 4); no se ha tocado `scripts/gate.sh`.
- `tests/corpus/` es dato de prueba versionado: se regenera con el generador, no se edita
  a mano.
- El corpus solo verifica lo que ejercita. Los contadores de cobertura del replay estan
  para que eso se vea, no para que se suponga.
