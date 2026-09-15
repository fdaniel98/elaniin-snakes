---
description: Lanza un A/B entre dos configs con el protocolo estadistico del proyecto
argument-hint: "<config_a.json> <config_b.json> --max-games <N>"
allowed-tools: Bash, Read
---

Config A: `$1`
Config B: `$2`
Resto de argumentos: `$ARGUMENTS`

Antes de lanzar nada, comprueba y di en voz alta:

1. `--max-games` es **obligatorio**. Si no esta en los argumentos, para y pidelo.
2. La unidad de analisis es el bloque (semilla por rotacion de asientos), no la partida.
3. A y B juegan partidas espejo con la misma composicion y la misma semilla, nunca la
   misma partida.
4. Metrica primaria unica: diferencia pareada de posicion media por bloque. El resto se
   reporta sin p-valores (ver docs/strategy.md#s-ab).
5. Agotado `--max-games` sin cruzar frontera, el veredicto es `NO CONCLUYENTE` y **no** se
   relanza con otras semillas: solo se amplia `--max-games` del mismo run.

Ejecucion:

!`./scripts/ab.sh $ARGUMENTS`

Reporta: veredicto, bloques consumidos, alfa, beta, delta y `--max-games` usados.
