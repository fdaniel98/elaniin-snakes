#!/usr/bin/env python3
"""Latencias que el ARBITRO reporto para cada snake, sacadas del JSONL de una partida.

    python3 scripts/latencias-jsonl.py docs/results/*.jsonl

Es la unica medicion que vale la vispera de un torneo. `curl` mide el transporte desde
esta maquina; esto mide lo que el arbitro VIO: si una peticion no llego a tiempo, el campo
`latency` viene vacio y ese turno la snake recibio el movimiento por defecto
(ver docs/rules.md#r-03), que suele matar.

El umbral no es un numero redondo: es el `timeout` real de la partida, que sale del propio
JSONL cuando esta, y 500 ms si no.
"""

import json
import pathlib
import sys


def percentil(xs, q):
    if not xs:
        return float("nan")
    s = sorted(xs)
    i = min(len(s) - 1, max(0, int(round(q * (len(s) - 1)))))
    return s[i]


def main():
    rutas = [pathlib.Path(a) for a in sys.argv[1:]]
    if not rutas:
        print("uso: latencias-jsonl.py <partida.jsonl> [...]", file=sys.stderr)
        raise SystemExit(2)

    por_snake, vacias, turnos_totales, timeout = {}, {}, 0, 500
    for ruta in rutas:
        for linea in ruta.read_text().splitlines():
            if not linea.strip():
                continue
            try:
                turno = json.loads(linea)
            except json.JSONDecodeError:
                continue
            if isinstance(turno.get("game"), dict):
                timeout = int(turno["game"].get("timeout", timeout) or timeout)
            n_turno = turno.get("turn")
            board = turno.get("board")
            snakes = board.get("snakes", []) if isinstance(board, dict) else []
            if not snakes:
                continue
            turnos_totales += 1
            # El turno 0 no tiene latencia: nadie ha contestado todavia. A partir de ahi,
            # un 0 es una medida legitima -el arbitro reporta milisegundos enteros y en
            # localhost la respuesta baja de 1 ms-, no un hueco. Descartarlo tiraba el 90%
            # de las muestras de una partida local.
            if n_turno == 0:
                continue
            for s in snakes:
                nombre = s.get("name", "?")
                bruto = s.get("latency", "")
                try:
                    ms = float(bruto)
                except (TypeError, ValueError):
                    vacias[nombre] = vacias.get(nombre, 0) + 1
                    continue
                por_snake.setdefault(nombre, []).append(ms)

    print(f"turnos leidos   {turnos_totales}")
    print(f"timeout         {timeout} ms")
    print()
    print(f"{'snake':<22} {'n':>6} {'p50':>7} {'p95':>7} {'p99':>7} {'max':>7} "
          f"{'>timeout':>9} {'sin dato':>9}")
    fallo = 0
    for nombre, xs in sorted(por_snake.items()):
        pasados = sum(1 for x in xs if x >= timeout)
        sin_dato = vacias.get(nombre, 0)
        print(f"{nombre:<22} {len(xs):>6} {percentil(xs,0.50):>7.1f} "
              f"{percentil(xs,0.95):>7.1f} {percentil(xs,0.99):>7.1f} {max(xs):>7.1f} "
              f"{pasados:>9} {sin_dato:>9}")
        if nombre == "nuestra" and (pasados or sin_dato):
            fallo = 1

    print()
    if fallo:
        print("FAIL nuestra snake tuvo turnos por encima del timeout o sin respuesta.")
        print("     Baja time.max_compute_ms y vuelve a desplegar.")
    else:
        print("OK nuestra snake contesto todos los turnos por debajo del timeout.")
    raise SystemExit(fallo)


if __name__ == "__main__":
    main()
