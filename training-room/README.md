# training-room/ - [FASE 3] orquestador, SQLite, ratings y reportes

Dos modos:

1. **Arena in-process** para velocidad y reproducibilidad (ver `arena/README.md`).
2. **Modo HTTP** para fidelidad, con el **CLI oficial como arbitro**
   (`battlesnake play`, royale, timeout 500 ms, salida JSONL). Aqui no hay
   reproducibilidad bit a bit: basta con persistir semilla y JSONL.

## Duelo 1v1 estandar contra el zoo (`duelo_http.py`)

El desempate del torneo es 1v1 estandar, y contra nosotros mismos el espejo ya esta
saturado. Esto mide el duelo contra codigo AJENO, por HTTP y con el arbitro oficial:

```bash
./scripts/zoo.sh build snork-tree            # una vez
python3 training-room/duelo_http.py --config snake/config/default.json \
    --rival snork-tree --semillas 20 --out docs/results/duelo-snork-v5
python3 training-room/compara.py --a docs/results/duelo-snork-v5 --b docs/results/duelo-snork-<cand>
```

Cada semilla son 2 partidas (asientos rotados). Por reloj, no por nodos: no es
reproducible bit a bit, asi que un veredicto necesita bastantes bloques.

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
