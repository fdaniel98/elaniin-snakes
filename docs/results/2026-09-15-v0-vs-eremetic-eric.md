# v0-baseline vs Eremetic Eric — 2026-09-15

Criterio 5 de la DoD de la fase 0: una partida completa del arbitro oficial contra una
snake publica del zoo, de principio a fin, con su JSONL guardado.

| Campo | Valor |
|---|---|
| JSONL | `2026-09-15-v0-vs-eremetic-eric.jsonl` (82 lineas: cabecera, 80 turnos y resultado) |
| Arbitro | CLI oficial, `battlesnake play -W 11 -H 11 -g royale -m royale -t 500` |
| Rival | Eremetic Eric, `coreyja/battlesnake-rs@d3a9bed`, contenedor aislado (ver zoo/README.md) |
| Ruleset leido del log | `royale`, `hazardDamagePerTurn: 14`, `shrinkEveryNTurns: 25`, `foodSpawnChance: 15` |
| Resultado | **gana `v0-baseline`**, sin empate; Eric eliminado en el turno 79 |

## Como se desarrollo

| turno | v0 salud/long | Eric salud/long | casillas de hazard |
|---|---|---|---|
| 0 | 100/3 | 100/3 | 0 |
| 30 | 99/8 | 72/4 | 11 |
| 50 | 61/9 | 52/4 | 21 |
| 70 | 80/13 | 32/4 | 21 |
| 78 | 99/14 | 10/4 | 31 |
| 79 | 98/14 | eliminada | 31 |

**Causa de la muerte de Eric: hambre, acelerada por el hazard.** Es un perseguidor de cola
que solo come cuando se muere de hambre: comio **una vez** en toda la partida y se quedo en
longitud 4 mientras su salud bajaba. En el turno 78 tenia la cabeza dentro de la zona de
hazard y perdio 15 de salud de golpe (1 por turno mas 14 de hazard), de 25 a 10; al turno
siguiente, a 0.

**v0 comio 11 veces** y llego a longitud 14, y en el turno 78 tenia la cabeza **fuera** de
la zona de hazard con 99 de salud. Los dos comportamientos que separan a las dos snakes son
justo los dos que `brain_v0` implementa a proposito: buscar comida por umbral de salud
(§8.3.5) y penalizar terminar el turno en hazard (§8.3.6).

## Que NO dice esta partida

Una partida no es una medicion. No hay veredicto estadistico, ni recursos pareados, ni
campo congelado: eso es la fase 3 y el protocolo de la skill `experiment-protocol`. Lo
unico que demuestra es que v0 juega una partida entera contra codigo que no es nuestro sin
romperse.
