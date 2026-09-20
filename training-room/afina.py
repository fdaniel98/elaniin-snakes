#!/usr/bin/env python3
"""Afina los pesos de la evaluacion con SPSA, en la arena.

    python3 training-room/afina.py --base snake/config/default.json \\
        --iteraciones 60 --bloques 8 --nodos 2000 --out docs/results/afinado-1

Los ~20 numeros de `snake/config/default.json` se pusieron a ojo y nunca se han tocado.
Esto los optimiza. Lo que salga es una PROPUESTA: pasa por el gauntlet como cualquier
otra (ver docs/decisions/ADR-0031-que-mide-la-arena.md#d-0311).

## La funcion objetivo, y por que es barata

El puesto medio de la candidata contra tres copias del campo, sobre una rotacion de
asientos completa. **Menos es mejor, y 2.5 es exactamente la nota del campo**: con los
cuatro contendientes iguales las cuatro partidas del bloque son la misma partida y los
puestos son 1, 2, 3 y 4 (ver docs/experimentos.md#s-tercer-rival-r). La referencia no hay
que jugarla, asi que cada evaluacion cuesta la mitad de lo que costaria un A/B.

## SPSA y por que SPSA

Dos evaluaciones por iteracion **cuesten los parametros que cuesten**: se perturban todos
a la vez con +-c y el gradiente se estima de la diferencia. Una busqueda por coordenadas
necesitaria dos evaluaciones POR PARAMETRO y aqui hay veinte.

Se optimiza en escala RELATIVA -x_i = p_i / p_i inicial, empezando en 1.0- porque los
pesos van de 2 a 120 y un paso absoluto que es un roce para uno es un salto para otro.

`space.weight` se queda FIJO como ancla. Multiplicar todos los pesos por una constante no
cambia casi nada -la busqueda compara valores- asi que sin ancla SPSA gastaria una
dimension entera en pasearse por esa escala.

Semillas COMUNES: todas las evaluaciones de una corrida usan los mismos bloques. Lo que se
compara entre dos evaluaciones es la configuracion, no la suerte.
"""

import argparse
import json
import pathlib
import subprocess
import sys
import time

RAIZ = pathlib.Path(__file__).resolve().parent.parent

# (grupo, clave, minimo, maximo, entero). Los limites son relativos al valor inicial y
# existen para que SPSA no se vaya a un sitio donde el parametro deja de significar nada.
TUNABLES = [
    ("territory", "weight", 0.3, 3.0, False),
    ("territory", "hazard_value_pct", 0.2, 2.0, True),
    ("head", "avoid_equal_or_longer", 0.3, 3.0, False),
    ("head", "prefer_shorter", 0.1, 5.0, False),
    ("hazard", "weight", 0.2, 4.0, False),
    ("hazard", "low_health_multiplier", 0.3, 3.0, False),
    ("length", "advantage_weight", 0.3, 3.0, False),
    ("length", "target_lead", 0.34, 3.0, True),
    ("length", "hunt_weight", 0.1, 5.0, False),
    ("food", "weight", 0.2, 5.0, False),
    ("food", "seek_below", 0.4, 1.8, True),
    ("food", "seek_below_in_hazard", 0.4, 1.3, True),
    ("food", "free_food_distance", 0.5, 3.0, True),
    ("search", "survival_bonus", 0.2, 4.0, False),
]

# `space.weight` es el ancla de escala y no se toca. `*.version`, `max_depth`,
# `max_rivals`, `death_value`, `win_value`, `reserve_us`, `budget_nodes` y todo `time`
# tampoco: no son pesos de evaluacion, son estructura y seguridad.
ANCLA = ("space", "weight")


def muere(msg):
    print(f"ERROR {msg}", file=sys.stderr)
    raise SystemExit(2)


def aplica(base, x):
    """Config nuevo = base con cada parametro multiplicado por su x relativo."""
    doc = json.loads(json.dumps(base))
    for (grupo, clave, lo, hi, entero), xi in zip(TUNABLES, x):
        v = base[grupo][clave] * xi
        doc[grupo][clave] = max(1, int(round(v))) if entero else round(v, 4)
    return doc


def evalua(binario, ruta_cand, ruta_campo, bloques, semilla_base, nodos, hilos):
    """Puesto medio de la candidata. Menos es mejor; 2.5 es la nota del campo."""
    cmd = [binario, "--a", str(ruta_cand), "--campo", str(ruta_campo),
           "--bloques", str(bloques), "--semilla-base", str(semilla_base),
           "--nodos", str(nodos)]
    if hilos > 0:
        cmd += ["--hilos", str(hilos)]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    if proc.returncode != 0:
        muere(f"arena_torneo salio con {proc.returncode}")
    filas = [json.loads(l) for l in proc.stdout.splitlines() if l.strip()]
    if not filas:
        muere("arena_torneo no devolvio partidas")
    if any(f["cortes_reloj"] for f in filas):
        muere("alguna partida la corto el reloj: la medicion no es reproducible")
    return sum(f["puesto"] for f in filas) / len(filas), len(filas)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default=str(RAIZ / "snake/config/default.json"))
    ap.add_argument("--campo", default=None, help="por defecto, el mismo que --base")
    ap.add_argument("--iteraciones", type=int, default=60)
    ap.add_argument("--bloques", type=int, default=8, help="bloques por evaluacion")
    ap.add_argument("--nodos", type=int, required=True)
    ap.add_argument("--hilos", type=int, default=0)
    ap.add_argument("--semilla-base", type=int, default=1000)
    ap.add_argument("--semilla-spsa", type=int, default=7)
    ap.add_argument("--c", type=float, default=0.15, help="perturbacion relativa inicial")
    ap.add_argument("--a", dest="paso", type=float, default=0.12, help="paso inicial")
    ap.add_argument("--out", required=True)
    ap.add_argument("--binario", default=str(RAIZ / "build/release/bin/arena_torneo"))
    args = ap.parse_args()

    if not pathlib.Path(args.binario).exists():
        muere(f"falta {args.binario}: compila el preset release")
    base = json.loads(pathlib.Path(args.base).read_text())
    campo = args.campo or args.base

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    diario = (out / "iteraciones.jsonl").open("w")
    tmp = out / "candidato.json"

    # Generador propio y explicito: `random` de Python cambia de algoritmo entre versiones
    # y esto tiene que poder repetirse. Lehmer de 64 bits, que para signos +-1 sobra.
    estado = args.semilla_spsa * 2 + 1

    def rademacher(n):
        nonlocal estado
        out_ = []
        for _ in range(n):
            estado = (estado * 6364136223846793005 + 1442695040888963407) % (1 << 64)
            out_.append(1.0 if (estado >> 33) & 1 else -1.0)
        return out_

    n = len(TUNABLES)
    x = [1.0] * n
    mejor_x, mejor_y = x[:], None
    t0 = time.time()
    partidas = 0

    print(f"afinando {n} parametros, {args.iteraciones} iteraciones x 2 evaluaciones x "
          f"{args.bloques * 4} partidas = {args.iteraciones * 2 * args.bloques * 4} partidas",
          file=sys.stderr)
    print(f"ancla fija: {ANCLA[0]}.{ANCLA[1]} = {base[ANCLA[0]][ANCLA[1]]}", file=sys.stderr)
    print("referencia: 2.500 es el puesto medio del campo, por construccion", file=sys.stderr)

    for k in range(1, args.iteraciones + 1):
        # Decaimiento estandar de SPSA (Spall): el paso y la perturbacion se encogen para
        # que la busqueda deje de saltar cuando ya esta cerca.
        ak = args.paso / (k + 10) ** 0.602
        ck = args.c / k ** 0.101
        delta = rademacher(n)

        def acota(v, lo, hi):
            return max(lo, min(hi, v))

        xmas = [acota(x[i] + ck * delta[i], TUNABLES[i][2], TUNABLES[i][3]) for i in range(n)]
        xmen = [acota(x[i] - ck * delta[i], TUNABLES[i][2], TUNABLES[i][3]) for i in range(n)]

        tmp.write_text(json.dumps(aplica(base, xmas), indent=2))
        ymas, np_ = evalua(args.binario, tmp, campo, args.bloques, args.semilla_base,
                           args.nodos, args.hilos)
        partidas += np_
        tmp.write_text(json.dumps(aplica(base, xmen), indent=2))
        ymen, np_ = evalua(args.binario, tmp, campo, args.bloques, args.semilla_base,
                           args.nodos, args.hilos)
        partidas += np_

        for i in range(n):
            g = (ymas - ymen) / (2.0 * ck * delta[i])
            x[i] = acota(x[i] - ak * g, TUNABLES[i][2], TUNABLES[i][3])

        y_medio = (ymas + ymen) / 2.0
        if mejor_y is None or y_medio < mejor_y:
            mejor_y, mejor_x = y_medio, x[:]

        fila = {"iteracion": k, "y_mas": round(ymas, 4), "y_menos": round(ymen, 4),
                "y_medio": round(y_medio, 4), "mejor": round(mejor_y, 4),
                "x": [round(v, 4) for v in x], "partidas": partidas,
                "segundos": round(time.time() - t0, 1)}
        diario.write(json.dumps(fila) + "\n")
        diario.flush()
        print(f"\ri{k}/{args.iteraciones}  y={y_medio:.3f}  mejor={mejor_y:.3f}  "
              f"{partidas} partidas  {(time.time() - t0) / 60:.0f} min ",
              end="", file=sys.stderr)

    print("", file=sys.stderr)
    diario.close()
    propuesta = out / "propuesta.json"
    doc = aplica(base, mejor_x)
    doc["_comment"] = (f"PROPUESTA de afinado SPSA ({args.iteraciones} iteraciones, "
                       f"{partidas} partidas, {args.nodos} nodos). Puesto medio en arena "
                       f"{mejor_y:.3f} contra 2.500 del campo. NO ENTRA en default.json "
                       f"sin ganar su A/B contra gauntlet-v1.")
    propuesta.write_text(json.dumps(doc, indent=2))

    print(f"\nmejor puesto medio {mejor_y:.4f} (campo = 2.5000)", file=sys.stderr)
    print("cambios sobre la base:", file=sys.stderr)
    for (grupo, clave, _lo, _hi, _e), xi in zip(TUNABLES, mejor_x):
        if abs(xi - 1.0) > 0.02:
            print(f"  {grupo}.{clave}: {base[grupo][clave]} -> {doc[grupo][clave]} "
                  f"(x{xi:.2f})", file=sys.stderr)
    print(f"\npropuesta en {propuesta}", file=sys.stderr)
    print("Confirmala con un A/B de arena contra la base, y si gana, con el gauntlet.",
          file=sys.stderr)


if __name__ == "__main__":
    main()
