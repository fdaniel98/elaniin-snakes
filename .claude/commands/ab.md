---
description: Lanza un A/B entre dos configs con el protocolo estadistico del proyecto
argument-hint: "<config_a.json> <config_b.json> --max-games <N>"
allowed-tools: Bash, Read
---

Config A: `$1`
Config B: `$2`
Argumentos completos, incluidos los dos de arriba: `$ARGUMENTS`

Antes de lanzar nada, comprueba y di en voz alta:

1. `--max-games` es **obligatorio**. Si no esta en los argumentos, para y pidelo.
2. El protocolo entero -unidad de analisis, partidas espejo, metrica primaria y que
   hacer al agotar el presupuesto- esta en un solo sitio: ver docs/strategy.md#s-ab.
   Leelo y aplicalo tal cual; no lo parafrasees aqui.

Ejecucion:

!`./scripts/ab.sh $ARGUMENTS`

Reporta: veredicto, bloques consumidos, alfa, beta, delta y `--max-games` usados.
