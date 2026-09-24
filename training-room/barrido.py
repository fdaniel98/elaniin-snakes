#!/usr/bin/env python3
"""Pasa TODOS los candidatos aparcados por el mismo tamiz, en el formato que se juega.

    python3 training-room/barrido.py --bloques 15 --out docs/results/barrido-standard

Por que existe: v6 y v13 se midieron en royale, y v14, v15 y v16 contra snork Tree, que
resulto ser un rival que nos saca una distancia enorme mientras al resto del campo le
ganamos (ver docs/experimentos-duelo.md#s-campo-duelo). Seis algoritmos escritos y
apagados, juzgados en el juego equivocado. Esto los vuelve a medir a todos en **standard**,
que es el formato del torneo (ver docs/strategy.md#s-formato).

Que NO es: un A/B contra el campo del zoo. Esto es la arena, o sea self-play con
presupuesto por nodos: barato, reproducible y suficiente para DESCARTAR. Lo que sobreviva
al tamiz se lleva su torneo contra `gauntlet-v2`, que es quien da veredictos.

Como se lee la tabla: con cuatro serpientes identicas el puesto medio es exactamente 2.500.
Un candidato que valga mas que v5 saca MENOS de 2.500 y su IC95 no lo cruza.
"""

import argparse
import collections
import json
import statistics as st
import subprocess
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
ARENA = RAIZ / "build/release/bin/arena_torneo"

# Cada entrada es (config, que enciende). El orden es el del historial, para que la tabla
# se lea como la lista de lo que quedo pendiente.
CANDIDATOS = [
    ("v6-turnos.json", "salud en turnos de vida (v6)"),
    ("v11-desesperacion.json", "no obedecer una raiz perdida (v11)"),
    ("v13-territorio-duelo.json", "territorio x2 en el duelo (v13)"),
    ("v14-trampa-duelo.json", "cuello umbralado en el duelo (v14)"),
    ("v15-supervivencia-duelo.json", "supervivencia en vez de area (v15)"),
    ("v16-tabla-duelo.json", "tabla de transposicion (v16)"),
    ("exp-sin-longitud.json", "CONTROL: v5 sin control de longitud"),
]


def corre(config, bloques, nodos, hilos, mapa, serpientes, salida):
    destino = salida / f"{Path(config).stem}.jsonl"
    if destino.exists() and destino.stat().st_size > 0:
        print(f"YA     {config}")
        return destino
    with open(destino, "w") as f:
        rc = subprocess.run(
            [str(ARENA), "--a", f"snake/config/{config}",
             "--campo", "snake/config/default.json",
             "--serpientes", str(serpientes), "--mapa", mapa,
             "--bloques", str(bloques), "--nodos", str(nodos), "--hilos", str(hilos)],
            cwd=RAIZ, stdout=f, stderr=subprocess.PIPE, text=True)
    if rc.returncode != 0:
        destino.unlink(missing_ok=True)
        print(f"ERROR  {config}: {rc.stderr.strip()[:300]}", file=sys.stderr)
        return None
    return destino


def lee(ruta):
    filas = [json.loads(l) for l in open(ruta, encoding="utf-8") if l.startswith("{")]
    por_bloque = collections.defaultdict(list)
    for x in filas:
        if x.get("rama") == "a":
            por_bloque[x["bloque"]].append(x["puesto"])
    medias = [st.mean(v) for _, v in sorted(por_bloque.items())]
    causas = collections.Counter(x["causa"] for x in filas if x.get("rama") == "a")
    return medias, causas, len([x for x in filas if x.get("rama") == "a"])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bloques", type=int, default=15)
    ap.add_argument("--nodos", type=int, default=14821,
                    help="presupuesto por movimiento; 14821 son los 150 ms de la de referencia")
    ap.add_argument("--hilos", type=int, default=2)
    ap.add_argument("--mapa", default="standard")
    ap.add_argument("--serpientes", type=int, default=4)
    ap.add_argument("--out", required=True)
    ap.add_argument("--solo", help="un unico config, por nombre de fichero")
    args = ap.parse_args()

    if not ARENA.exists():
        sys.exit(f"falta {ARENA}; corre 'cmake --build --preset release'")
    salida = Path(args.out)
    salida.mkdir(parents=True, exist_ok=True)

    lista = [c for c in CANDIDATOS if not args.solo or c[0] == args.solo]
    if not lista:
        sys.exit(f"ningun candidato se llama {args.solo}")
    neutro = (args.serpientes + 1) / 2

    print(f"barrido en {args.mapa} de {args.serpientes}, {args.bloques} bloques, "
          f"{args.nodos} nodos. Neutro = {neutro:.3f}\n")
    resultados = []
    for config, que in lista:
        t0 = time.time()
        ruta = corre(config, args.bloques, args.nodos, args.hilos, args.mapa,
                     args.serpientes, salida)
        if ruta is None:
            continue
        medias, causas, n = lee(ruta)
        if not medias:
            print(f"ERROR  {config} no dejo partidas")
            continue
        m = st.mean(medias)
        ic = 1.96 * st.stdev(medias) / len(medias) ** 0.5 if len(medias) > 1 else 0.0
        veredicto = ("MEJOR" if m + ic < neutro else
                     "PEOR" if m - ic > neutro else "SIN DIFERENCIA")
        resultados.append((config, que, m, ic, n, veredicto, causas))
        print(f"{config:<32} {m:.3f} [{m - ic:.3f}, {m + ic:.3f}]  {veredicto}"
              f"   ({n} partidas, {(time.time() - t0) / 60:.0f} min)")

    print(f"\n{'candidato':<32}{'puesto medio':>14}{'IC95':>22}  veredicto")
    for config, que, m, ic, n, veredicto, _ in sorted(resultados, key=lambda r: r[2]):
        print(f"{config:<32}{m:>14.3f}{f'[{m - ic:.3f}, {m + ic:.3f}]':>22}  {veredicto}")
    print(f"\nneutro (cuatro iguales) = {neutro:.3f}. MENOS es mejor.")
    print("Lo que salga MEJOR se lleva su torneo contra gauntlet-v2; lo demas se queda apagado.")
    print("\ncausas de muerte por candidato:")
    for config, _, _, _, _, _, causas in resultados:
        print(f"  {config:<32}{dict(causas)}")


if __name__ == "__main__":
    main()
