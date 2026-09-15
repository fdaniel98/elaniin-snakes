#!/usr/bin/env bash
# [FASE 4] A/B entre dos configs con el protocolo estadistico del prompt maestro:
# unidad de analisis = bloque (semilla x rotacion de asientos), metrica primaria unica
# (diferencia pareada de posicion media), SPRT con alfa, beta, delta y --max-games
# declarados en el config y escritos en el reporte.
#
#   ./scripts/ab.sh --a config_a.json --b config_b.json --gauntlet gauntlet-v1.json #                   --sprt --max-games N
#
# Requisitos que ya estan decididos y que esta fase NO puede saltarse:
#   - A y B nunca juegan la misma partida: se comparan en partidas espejo.
#   - El schedule de comida y hazards se pre-genera desde la semilla, indexado por turno.
#   - Agotado --max-games sin cruzar frontera el veredicto es NO CONCLUYENTE, y NO se
#     relanza con otras semillas: solo se amplia --max-games del mismo run.
set -uo pipefail

echo "no implementado: fase 4 (arena in-process + A/B)"
echo "ver docs/strategy.md#s-ab y docs/context-packs/nueva-heuristica.md"
exit 3
