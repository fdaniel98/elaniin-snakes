# arena/ - [FASE 4] self-play in-process

Enfrenta variantes compiladas del cerebro **sin HTTP**, con presupuesto determinista por
nodos (`budget_nodes`) y nunca por reloj. El motivo, en la skill `experiment-protocol`,
apartado de lo que invalida un resultado.

## Contrato previsto

La firma real esta en `arena/include/arena/arena.hpp`. El presupuesto por nodos no vive en
`ArenaConfig` sino en `Params::search::budget_nodes`, que es donde lo lee la busqueda y
donde puede ser distinto por contendiente. El modelo de shrink todavia no es un parametro:
la arena regenera los hazards con `royale_hazards()` y el cerebro los ve en el tablero.

- El payload de `/move` no trae la semilla, asi que en partida real el lado del proximo
  shrink no es conocible (ver docs/rules.md#r-09). Dentro de la arena si lo es, y modelarlo
  ahi seria medir una snake que no se puede desplegar: por eso el cerebro sigue viendo solo
  el tablero.
- El sorteo de comida y hazards **no se materializa**: se indexa por turno, sembrando un
  `Rng` con `semilla + turno` en cada llamada. Las casillas no se pueden fijar por
  adelantado porque dependen de la ocupacion, que depende de la partida; lo que se fija son
  los numeros. ver docs/decisions/ADR-0029-schedule-por-turno.md#d-0291
- Usa el mismo `decide()` que el servidor (ver docs/architecture.md#a-02).

## Lo que mide y lo que no

Aqui solo juegan configuraciones NUESTRAS: las del zoo son contenedores HTTP. Un veredicto
de arena es sobre self-play y **nada entra en `default.json` por el**; la puerta sigue
siendo el A/B por HTTP contra `gauntlet-v1`. La arena sirve para triaje y para afinado.
ver docs/decisions/ADR-0031-que-mide-la-arena.md#d-0311

Y es rapida porque juega con menos presupuesto por movimiento, no porque quite el
transporte: ver docs/performance.md#p-08.

## Estado

Implementada y usable:

- `arena::play()` juega una partida entera y es reproducible.
- `bin/arena_torneo` juega las dos ramas de un A/B en la misma corrida, en paralelo. Las
  partidas se escriben por indice, asi que la salida es identica con 1 hilo y con 16.
- `training-room/arena_ab.py` las vuelca en dos bases con el esquema de `tr.py` y el
  veredicto lo da `compara.py`, que sigue siendo la unica ruta estadistica del repositorio.
- `bin/sonda_arena --calibrar` dice cuantos nodos caben en `time.max_compute_ms` en ESTA
  maquina, que es lo primero que hay que correr antes de montar un A/B.

```bash
./build/release/bin/sonda_arena --calibrar
python3 training-room/arena_ab.py --a <config> --b <config> --campo snake/config/default.json \
    --bloques 15 --nodos <el que diga --calibrar> --out docs/results/arena-<nombre>
```

Falta el loop de la fase 4 (ver docs/decisions/ADR-0032-arena-en-el-ambito-del-loop.md#d-0322)
y la DoD: test A/A, regresion inyectada y verificacion del pareado.
