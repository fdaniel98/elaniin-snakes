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
  Por defecto deja **dos nucleos libres**: estas corridas duran horas y la maquina tiene
  que poder usarse. `--hilos N` pide un numero exacto, y `renice -n 19` sobre una corrida
  ya lanzada la manda al fondo de la cola sin matarla.
- `training-room/arena_ab.py` las vuelca en dos bases con el esquema de `tr.py` y el
  veredicto lo da `compara.py`, que sigue siendo la unica ruta estadistica del repositorio.
- `bin/sonda_arena --calibrar` dice cuantos nodos caben en `time.max_compute_ms` en ESTA
  maquina, que es lo primero que hay que correr antes de montar un A/B.

```bash
./build/release/bin/sonda_arena --calibrar     # en la de referencia: 19761
python3 training-room/arena_ab.py \
    --a snake/config/v7-rivales3.json \
    --b snake/config/default.json \
    --campo snake/config/default.json \
    --bloques 15 --nodos 19761 \
    --out docs/results/arena-v7
```

El numero de `--nodos` se escribe entero, no entre `<>`: en bash eso es una redireccion y
el comando muere con `No such file or directory` antes de empezar. La cifra de la maquina
de referencia esta en ver docs/performance.md#p-08.

Falta el loop de la fase 4 (ver docs/decisions/ADR-0032-arena-en-el-ambito-del-loop.md#d-0322)
y la DoD: test A/A, regresion inyectada y verificacion del pareado.
