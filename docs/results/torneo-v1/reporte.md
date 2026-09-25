---
title: "Torneo v0-baseline contra gauntlet-v1"
read_when: "antes de comparar una version nueva contra la linea base del campo congelado"
authority: canonical
last_verified: 2026-09-18
---


## T-01 Que se midio {#t-01}

| campo | valor |
|---|---|
| campo congelado | `gauntlet-v1` |
| partidas jugadas / con el arbitro en error | 200 / 0 |
| bloques (semillas distintas) | 50 |
| commit de nuestra snake | `a74c2090a42a` |
| hash del config | `f9e4d0e1b611ba45` |
| turnos por partida (media / min / max) | 175.3 / 34 / 272 |
| nucleos / hilos por nucleo / governor | 8 / 1 / desconocido |

Dos corridas con nucleos, hilos por nucleo o governor distintos **no se comparan**.
El governor sale `desconocido` en WSL2 porque no expone `cpufreq`: la frecuencia la
gobierna el anfitrion y no se puede ni leer ni fijar desde aqui.

## T-02 Clasificacion {#t-02}

| snake | puesto medio | primeros | partidas | turnos vividos |
|---|---|---|---|---|
| hovering-hobbs | 1.295 | 161 | 200 | 173.1 |
| devious-devin | 2.152 | 32 | 200 | 162.9 |
| v0-baseline | 2.770 | 6 | 200 | 119.2 |
| eremetic-eric | 3.783 | 1 | 200 | 58.4 |

Puesto medio con **rango compartido promediado** para las eliminadas en el mismo
turno, nunca por indice de asiento (ver docs/rules.md#r-12).

## T-03 Efecto del asiento {#t-03}

La rotacion existe para que el asiento no se confunda con la snake. Si funciona,
estas filas son planas; si no lo son, el resto del reporte no vale.

| snake | asiento 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| devious-devin | 2.100 | 2.190 | 2.220 | 2.100 |
| eremetic-eric | 3.720 | 3.840 | 3.800 | 3.770 |
| hovering-hobbs | 1.300 | 1.340 | 1.420 | 1.120 |
| v0-baseline | 2.820 | 2.800 | 2.790 | 2.670 |

## T-04 Latencia y timeouts {#t-04}

| snake | p50 | p95 | p99 | maximo | timeouts | movimientos | tasa |
|---|---|---|---|---|---|---|---|
| devious-devin | 406.7 | 428.5 | 449.0 | 2321 | 192 | 32545 | 0.590 % |
| hovering-hobbs | 362.4 | 384.9 | 404.1 | 2321 | 172 | 34449 | 0.499 % |
| eremetic-eric | 0.0 | 3.7 | 28.1 | 501 | 12 | 11675 | 0.103 % |
| v0-baseline | 0.0 | 1.4 | 10.5 | 501 | 8 | 23831 | 0.034 % |

Milisegundos, medidos **por el arbitro**, que es quien decide si una respuesta llego
a tiempo. Los timeouts salen de su stderr y no del JSONL: la serpiente eliminada
desaparece del turno siguiente y el ultimo turno no se exporta, asi que contarlos en
el JSONL perderia justo los que importan (ver docs/rules-parametros.md#r-20).
Los fallos de `/end` **no** se cuentan: no cuestan un movimiento.

## T-05 Causas de muerte {#t-05}

| snake | cabezazo | cuerpo_propio | cuerpo_rival | hambre | hazard | ambigua | modelo_discrepa | sobrevivio | total |
|---|---|---|---|---|---|---|---|---|---|
| devious-devin | 0 | 11 | 0 | 20 | 8 | 129 | 0 | 32 | 200 |
| eremetic-eric | 0 | 0 | 0 | 8 | 0 | 191 | 0 | 1 | 200 |
| hovering-hobbs | 0 | 5 | 0 | 3 | 2 | 29 | 0 | 161 | 200 |
| v0-baseline | 78 | 20 | 17 | 11 | 67 | 0 | 1 | 6 | 200 |

**De nuestras 194 muertes, 193 estan determinadas.**
El JSONL no exporta los movimientos, asi que el de una serpiente que muere se
enumera y se queda con los candidatos que reproducen el turno siguiente
observado; cuando varios llevan a causas distintas, es `ambigua` y se cuenta como
tal. Para la nuestra hay atajo: el cerebro es determinista, asi que se le
pregunta. El movimiento modelado tiene que estar entre los candidatos
consistentes o la fila sale `modelo_discrepa`, que seria un hallazgo -el replay
creyendo que hicimos algo que no hicimos- y no un detalle a tapar.

Las de los rivales siguen siendo ambiguas en su mayoria y asi se quedan: no
tenemos su cerebro, y elegir la causa mas probable seria inventar un dato.

## T-06 Lo que este reporte no dice {#t-06}

- **No hay veredicto ni p-valores.** Todo lo de arriba es descriptivo. La metrica
  con test es la diferencia pareada de posicion media por bloque, y ese es el A/B de
  la fase 4.
- **El campo mide menos de lo que parece.** Las tres rivales salen del mismo binario,
  asi que sus errores estan correlacionados.
- **No hay reproducibilidad bit a bit.** El arbitro oficial usa el `math/rand` de Go.
  Se persisten la semilla y el JSONL de cada partida y nada mas
  (ver docs/decisions/ADR-0016-reproducibilidad-de-la-arena.md#d-0151).

