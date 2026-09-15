# training-room/ - [FASE 3] orquestador, SQLite, ratings y reportes

Dos modos:

1. **Arena in-process** para velocidad y reproducibilidad (ver `arena/README.md`).
2. **Modo HTTP** para fidelidad, con el **CLI oficial como arbitro**
   (`battlesnake play`, royale, timeout 500 ms, salida JSONL). Aqui no hay
   reproducibilidad bit a bit: basta con persistir semilla y JSONL.

## Comandos previstos

```bash
training-room match --snakes nuestra@v2,<oponentes> --games 200
training-room ab --a config_a.json --b config_b.json --gauntlet gauntlet-v1.json --sprt --max-games N
training-room report --last 7d
```

## Que persiste cada partida (SQLite)

Version y commit de cada snake, hash del config, semilla de 64 bits, version del `Rng`,
`gauntlet` usado, ruta al JSONL, posiciones finales, causa de muerte, y latencia
p50/p95/p99/max por movimiento.

Los empates se puntuan con rango compartido promediado, nunca por indice de asiento
(ver docs/rules.md#r-12). El rating multijugador (OpenSkill o TrueSkill) es **descriptivo**:
no decide A/B.

Estado: no implementado. Lo abre la fase 3.
