---
title: "ADR-0018: el orquestador es Python; la arena sigue siendo C++"
read_when: "antes de anadir codigo a training-room/ o de discutir por que no es C++"
authority: canonical
last_verified: 2026-09-18
size_bytes: 2729
---


## D-0170 Contexto {#d-0170}

`training-room/` orquesta partidas del arbitro oficial contra contenedores, persiste en
SQLite y emite reportes. Todo lo que hace es esperar a procesos ajenos: `docker run`, el
CLI del arbitro, y lecturas de JSONL al final.

El resto del proyecto es C++20 por una razon concreta: el motor y el cerebro estan en la
ruta caliente de un deadline de 350 ms. El orquestador no esta en ninguna ruta caliente.
Su rendimiento se mide en partidas por minuto, y ese numero lo fijan el `docker build`, el
arranque de los contenedores y los 500 ms de timeout por movimiento, no el lenguaje del
bucle que los lanza.

## D-0171 Decision {#d-0171}

`training-room/` se escribe en Python 3, sin dependencias fuera de la biblioteca estandar
(`sqlite3`, `json`, `subprocess`, `argparse`).

La **arena in-process de la fase 4 sigue siendo C++**, enlazada contra `engine/` y
`snake/`, y no se toca por esto: alli el presupuesto es por nodos, se juegan miles de
partidas por minuto y el coste del lenguaje si aparece en el resultado.

La frontera queda asi: **lo que juega es C++, lo que organiza es Python.**

## D-0172 Alternativas descartadas {#d-0172}

| Alternativa | Por que no |
|---|---|
| Orquestador en C++ | Coherencia de lenguaje a cambio de escribir manejo de procesos, parseo de JSON y SQLite a mano, para un componente que pasa el 99% del tiempo bloqueado esperando a `docker` y al arbitro |
| Bash, como `soak.sh` | Funciona para un bucle de 200 partidas con tres rivales fijos, y ya se llego al limite: `soak.sh` tuvo que delegar el analisis en `soak_analiza.py` porque en bash no se podian calcular percentiles sin mentir. Aqui ademas hay SQLite y rotacion de asientos |
| Rust | Otro toolchain mas que instalar en la maquina de referencia y en CI, sin ganancia medible en un componente que espera |

## D-0173 Consecuencias {#d-0173}

- `training-room/` esta dentro de `closing.deliverable_scope`
  (ver docs/decisions/ADR-0017-el-instrumento-lleva-loop.md#d-0161), asi que lleva loop
  completo aunque sea Python. La clase `perf` de ese loop mide **partidas por minuto**, no
  microsegundos: el umbral que aplica es el throughput, no la latencia.
- Sin dependencias externas: `bootstrap.sh` ya verifica `python3`, y no hay que anadir un
  gestor de paquetes al gate.
- Si algun dia el orquestador aparece en un perfil como cuello de botella, eso seria un
  hallazgo medido y no una intuicion, y entonces se reabre esta decision con el numero
  delante (regla de oro 2).
