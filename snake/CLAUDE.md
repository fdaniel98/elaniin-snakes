# snake/ - invariantes locales

Cerebro y servidor. El cerebro es una libreria (`snake_brain`) para que la arena y los
tests usen exactamente el mismo `decide` que el servidor (ver docs/architecture.md#a-02).

## Contrato del cerebro

- `decide()` es `noexcept`: pase lo que pase, devuelve un movimiento.
- Jamas una direccion inmediatamente mortal si existe otra que no lo sea
  (ver docs/invariants.md#inv-10).
- Jamas excede el deadline, ni con un presupuesto de 5 ms
  (ver docs/invariants.md#inv-11).
- Fail-safe en cuatro escalones, y cada uno tiene su test:
  `0` decision normal, `1` movimiento seguro con mas espacio (deadline vencido),
  `2` cualquier movimiento dentro del tablero, `3` `up`.
- Ningun peso de estrategia vive en el codigo: todos salen de `Params`, que es 1:1 con
  `snake/config/default.json`. Si divergen, `tests/test_ruleset_parse.cpp` falla.

## Contrato del servidor

- Escucha en `0.0.0.0:$PORT` (8080 por defecto), cuerpo maximo 256 KiB, timeouts de
  lectura y escritura explicitos.
- `GET /health` responde 200 **sin invocar el cerebro**: el `GET /` de personalizacion lo
  consume el motor y no sirve como sonda.
- Rutas desconocidas: 404 sin parsear el cuerpo.
- `/start` y `/end` aguantan lo mismo que `/move`: un JSON que no sea objeto no puede
  darles 500. Habia uno, y duro hasta la fase 2 porque el check 8 solo probaba `/move`.
- Un `set_exception_handler` cubre **toda** ruta: cpp-httplib responde 500 por su cuenta
  si un handler lanza, y de paso filtra el mensaje de la excepcion en una cabecera.
- Cuerpo maximo 256 KiB, y timeouts de socket cortos: con 5 s bastaban ocho conexiones a
  medio abrir para agotar el pool de hilos y dejar sin responder, que es peor que un 5xx.
- `POST /move` **nunca** devuelve 5xx (ver docs/invariants.md#inv-12): payload invalido,
  tablero de otro tamaño o serpiente propia ausente caen al fail-safe con `WARN`. El
  check 8 lo comprueba con 14 payloads adversos, no solo con fixtures validos.
- **El servidor juega 11x11 y nada mas.** El motor conoce tres tamaños, el cerebro uno;
  en cualquier otro se responde el ultimo escalon del fail-safe. Es una decision, no una
  carencia: ver docs/decisions/ADR-0014-el-servidor-es-11x11.md#d-0131.
- Una linea de log por movimiento, parseable: turno, ruleset, escalon del fail-safe,
  candidatos, valor y microsegundos.

## Variantes

El cerebro declara soporte para `standard` y `royale`. Cualquier otra entra en
**modo degradado seguro**: sin tail-escape ni modelo de hazards, solo filtro duro y flood
fill conservador, con `WARN` en el log. En `constrictor` el tail-escape es directamente
mortal porque la cola no avanza jamas (ver docs/rules.md#r-04).

## Antes de tocar la evaluacion

Lee `docs/context-packs/nueva-heuristica.md`. `brain_v0` **no se modifica**: es la
referencia fija contra la que se mide todo lo demas (ver docs/strategy.md#s-v0). Una
heuristica nueva es una version nueva.
