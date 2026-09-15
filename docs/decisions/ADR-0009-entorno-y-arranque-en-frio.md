---
title: "ADR-0009: el entorno roto no es un veredicto, y el arranque en frio se mide aparte"
read_when: "al interpretar un gate en rojo, o antes de tocar el check 8 y sus umbrales"
authority: canonical
last_verified: 2026-09-15
size_bytes: 3190
---


## D-0080 Contexto {#d-0080}

La primera corrida del gate completo en la maquina de referencia dejo tres checks en rojo
y los 22 venenos fallidos. Dos causas, no cinco:

1. **git rechazaba el repositorio** (`dubious ownership`: el repo vive en `/mnt/c` y su
   propietario no es quien ejecuta). Media docena de comprobaciones llaman a git
   -`ls-files`, `cat-file`, `archive`, `merge-base`-, asi que cayeron el check 6, el 9 y
   la autoprueba entera. Peor que caer: **mintieron**. El check 6 dijo «hechos duplicados
   por encima del umbral» cuando lo que habia pasado es que `git ls-files` salio 128, y
   el check 9 acuso a los ledgers de tener commits inexistentes.
2. **El check 8 medio 179.77 ms de maximo** frente a un umbral de 150, con p50 de 0.45 ms
   y p99 de 0.98 ms. El maximo era la primera peticion: arranque del proceso y carga del
   config, no el cerebro decidiendo.

## D-0081 Decision {#d-0081}

**Entorno.** `gate.sh` y `gate-selftest.sh` empiezan comprobando `git rev-parse HEAD`. Si
falla, imprimen el error real, la linea exacta que lo arregla, y salen con **codigo 2**,
que la especificacion ya reservaba para «error de entorno o de toolchain, que no es un
veredicto sobre el codigo». `scripts/lint_dupes.py` hace lo mismo: un fallo de git sale 2,
nunca 1, porque salir 1 significa «hay duplicados» y eso era falso.

**Arranque en frio.** El smoke manda una peticion de calentamiento -real, con su
movimiento comprobado como las demas-, la excluye de la muestra y publica su latencia como
`arranque_en_frio`, con umbral propio `cold_start_ms_max` = 500 ms en `config/loop.json`.
El maximo de las demas sigue con su techo de 150 ms.

Aprobado por el humano el 2026-09-15, enunciada la alternativa de subir el umbral.

## D-0082 Alternativas descartadas {#d-0082}

| Alternativa | Por que no |
|---|---|
| Subir `max_move_ms_max` de 150 a 250 | Relaja lo comprobado y deja el arranque en frio escondido dentro de un numero que ya no distingue un cerebro lento de un proceso recien nacido |
| Descartar la primera peticion sin medirla | Mide menos que hoy. El arranque en frio importa de verdad en la fase 7, donde una instancia fria contesta una partida real |
| Dejar el check 8 en rojo hasta la fase 2 | El numero que falla no mide lo que el check dice medir; arreglar la medicion no es relajarla |
| Solo documentar que hay que correr `git config --global --add safe.directory` | Una nota en un README no evita que el gate vuelva a acusar al codigo de un fallo del entorno |

## D-0083 Consecuencias {#d-0083}

- Tres checks pasan de acusar al codigo a decir que falta configurar git, con la linea
  exacta para arreglarlo.
- `cold_start_ms_max` es un umbral nuevo: el gate comprueba **mas** que antes, no menos.
  Queda sin veneno propio -el del check 8 cubre el movimiento ilegal-, y eso es deuda
  anotada en `STATE.md`.
- `config/loop.json` cambia, asi que los tres ledgers copian su sha256 nuevo.
