---
title: "ADR-0015: el presupuesto por tarea sube a 64 KB porque el harness esta hecho para crecer"
read_when: "antes de tocar un presupuesto de bytes o de anadir un veneno al gate"
authority: canonical
last_verified: 2026-09-18
size_bytes: 3137
---


## D-0140 Contexto {#d-0140}

El presupuesto por tarea son 48 KB e incluye el arranque, el context pack y los archivos
que ese pack lista (ver docs/INDEX.md#i-02). El pack del harness lista `docs/harness.md`,
`scripts/gate.sh`, `scripts/gate-selftest.sh` y `config/loop.json`.

`scripts/gate-selftest.sh` crece unos 640 bytes por veneno, y **anadir un veneno es
obligatorio cada vez que se endurece un check** (regla de oro 4). Cuando se fijo el techo
habia 12 venenos; hay 27. El pack se pasa por 1 KB, y en la fase siguiente se pasara por
mas.

O sea: dos reglas del proyecto tirando en direcciones contrarias. El gate solo crece, y el
presupuesto no sube. La colision no es un accidente, es aritmetica.

Ya se resolvio dos veces por la via mala. La primera, sacando `config/loop.json` de la
tabla de carga del pack: eso no reduce lo que la tarea tiene que abrir, solo lo que
`docs_meta.sh --budget` mide, porque el presupuesto cuenta los archivos entre los
marcadores. Lo caza la auditoria de la fase 2 y se revirtio. La segunda, recortando
`STATE.md`, que si vale -lo carga toda tarea- pero que no da mas de si.

## D-0141 Decision {#d-0141}

El presupuesto **por tarea** sube de 49152 a **65536 bytes**.

Aprobado por el humano el 2026-09-18, enunciadas las alternativas de partir el selftest y
de sacarlo del pack.

El presupuesto de **arranque** -`CLAUDE.md` + `STATE.md` + `docs/INDEX.md`- **no cambia**:
sigue en 12288 bytes. Es el que protege lo que de verdad importa, que una sesion nueva
reconstruya el estado mental sin leer medio repositorio; el de tarea protege una lectura
que ya viene dirigida por un pack.

## D-0142 Alternativas descartadas {#d-0142}

| Alternativa | Por que no |
|---|---|
| Partir `gate-selftest.sh` en runner y venenos | Modularizacion de verdad y deja el techo intacto, pero el pack tendria que explicar cuando abrir el segundo archivo, y la tarea sigue necesitando los dos. Mueve el problema de sitio |
| Sacar el selftest del pack | Es exactamente la jugada que la auditoria de la fase 2 señalo con `config/loop.json`: el numero pasa y la tarea abre lo mismo |
| Seguir recortando `STATE.md` | Ya se hizo dos veces y da de si unos cientos de bytes. La siguiente fase vuelve a pasarse |
| Dejar de anadir venenos | Es la regla de oro 4 al reves: un check endurecido sin veneno es una afirmacion, no una comprobacion |

## D-0143 Consecuencias {#d-0143}

- Un pack puede ahora pedir 64 KB, que a bytes/4 son unos 16000 tokens de arranque
  dirigido. Sigue siendo una fraccion de una ventana de contexto y sigue siendo un techo,
  no una barra libre: `docs_meta.sh --budget` lo comprueba igual.
- El arranque sigue en 12288, que es el numero que de verdad protege una sesion nueva.
- Si el harness vuelve a pasarse, la respuesta ya no es subir otra vez: sera partir el
  selftest, que es la alternativa que este ADR deja descrita y sin hacer.
