#!/usr/bin/env python3
"""A/B en la arena: dos configuraciones contra el mismo campo, en los mismos bloques.

    python3 training-room/arena_ab.py --a snake/config/v7-rivales3.json \\
        --b snake/config/default.json --campo snake/config/default.json \\
        --bloques 15 --nodos 2000 --out docs/results/arena-v7

Que hace y que no:

- **No arbitra ni cuenta nada.** Llama a `bin/arena_torneo`, que juega, y escribe lo que
  devuelve en dos bases con el MISMO esquema que `tr.py`. El veredicto lo da `compara.py`,
  que es la unica ruta estadistica del repositorio.
- **Las dos ramas salen de la misma corrida**, no de dos. Asi comparten bloque, semilla y
  asiento por construccion, que es lo que hace pareada la comparacion, en vez de por
  acordarse de pasar el mismo `--seed-base`.

Un veredicto de arena es sobre SELF-PLAY y no sobre el campo del gauntlet: nada entra en
`default.json` por el. Ver docs/decisions/ADR-0031-que-mide-la-arena.md
"""

import argparse
import hashlib
import json
import os
import pathlib
import sqlite3
import subprocess
import sys
import time

RAIZ = pathlib.Path(__file__).resolve().parent.parent

ESQUEMA = """
CREATE TABLE IF NOT EXISTS partidas (
  id TEXT PRIMARY KEY, gauntlet TEXT NOT NULL, semilla INTEGER NOT NULL,
  jsonl TEXT NOT NULL, asiento_nuestro INTEGER NOT NULL, turnos INTEGER,
  arbitro_rc INTEGER NOT NULL, empezada_en TEXT NOT NULL,
  topologia TEXT NOT NULL, rng_version TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS participantes (
  partida_id TEXT NOT NULL REFERENCES partidas(id), slug TEXT NOT NULL,
  nombre TEXT NOT NULL, version TEXT, commit_snake TEXT, hash_config TEXT,
  imagen TEXT, asiento INTEGER NOT NULL, puesto REAL, turnos_sobrevividos INTEGER,
  causa_muerte TEXT, PRIMARY KEY (partida_id, slug));
CREATE TABLE IF NOT EXISTS latencias (
  partida_id TEXT NOT NULL REFERENCES partidas(id), slug TEXT NOT NULL,
  p50 REAL, p95 REAL, p99 REAL, maximo REAL, timeouts INTEGER, movimientos INTEGER,
  fallos_status INTEGER, fallos_json INTEGER, fallos_movimiento INTEGER,
  fallos_conexion INTEGER,
  PRIMARY KEY (partida_id, slug));
"""


def muere(msg):
    print(f"ERROR {msg}", file=sys.stderr)
    raise SystemExit(2)


def hash_config(ruta):
    return hashlib.sha256(pathlib.Path(ruta).read_bytes()).hexdigest()[:12]


def commit_actual():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=RAIZ,
                              capture_output=True, text=True, timeout=10).stdout.strip()
    except Exception:
        return "desconocido"


def escribe(ruta, filas, meta, slug, hconf):
    ruta.parent.mkdir(parents=True, exist_ok=True)
    if ruta.exists():
        ruta.unlink()
    db = sqlite3.connect(ruta)
    db.executescript(ESQUEMA)
    for f in filas:
        pid = f"{f['bloque']:05d}-{f['asiento']}"
        db.execute(
            "INSERT INTO partidas VALUES (?,?,?,?,?,?,?,?,?,?)",
            (pid, meta["gauntlet"], f["semilla"], "", f["asiento"], f["turnos"],
             0 if f["final"] == "ok" else 1, meta["empezada_en"],
             json.dumps(meta["topologia"], sort_keys=True), meta["rng_version"]))
        db.execute(
            "INSERT INTO participantes VALUES (?,?,?,?,?,?,?,?,?,?,?)",
            (pid, slug, slug, meta["version"], meta["commit"], hconf,
             f"arena/{slug}", f["asiento"], f["puesto"], f["turnos_vividos"],
             f["causa"]))
    db.commit()
    db.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", required=True, help="rama de referencia")
    ap.add_argument("--b", required=True, help="rama candidata")
    ap.add_argument("--campo", required=True,
                    help="config de las otras tres sillas; igual para las dos ramas")
    ap.add_argument("--bloques", type=int, default=15)
    ap.add_argument("--semilla-base", type=int, default=1)
    ap.add_argument("--nodos", type=int, required=True,
                    help="presupuesto por movimiento; obligatorio y sin default a "
                         "proposito: un numero de nodos no es comparable entre commits "
                         "que cambian el coste del nodo (ADR-0030)")
    ap.add_argument("--hilos", type=int, default=0, help="0 = todos los nucleos")
    ap.add_argument("--out", required=True)
    ap.add_argument("--binario", default=str(RAIZ / "build/release/bin/arena_torneo"))
    args = ap.parse_args()

    if not pathlib.Path(args.binario).exists():
        muere(f"falta {args.binario}: compila el preset release primero")
    for r in (args.a, args.b, args.campo):
        if not pathlib.Path(r).exists():
            muere(f"no existe el config {r}")

    ha, hb = hash_config(args.a), hash_config(args.b)
    if ha == hb:
        muere(f"--a y --b son el MISMO config (hash {ha}). Un A/A se hace a proposito, "
              "apuntando a dos ficheros distintos con el mismo contenido.")

    cmd = [args.binario, "--a", args.a, "--b", args.b, "--campo", args.campo,
           "--bloques", str(args.bloques), "--semilla-base", str(args.semilla_base),
           "--nodos", str(args.nodos)]
    if args.hilos > 0:
        cmd += ["--hilos", str(args.hilos)]

    t0 = time.time()
    print(" ".join(cmd), file=sys.stderr)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        muere(f"arena_torneo salio con {proc.returncode}:\n{proc.stderr[:2000]}")
    filas = [json.loads(l) for l in proc.stdout.splitlines() if l.strip()]
    if not filas:
        muere("arena_torneo no devolvio ninguna partida")
    segundos = time.time() - t0

    # Una sola partida cortada por el RELOJ invalida la corrida entera: con presupuesto
    # por nodos el reloj no debe participar, y si participo el resultado depende de la
    # carga de la maquina. ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0301
    sucias = [f for f in filas if f["cortes_reloj"] > 0]
    if sucias:
        muere(f"{len(sucias)} partidas de {len(filas)} las corto el RELOJ y no el "
              "presupuesto de nodos: la corrida no es reproducible y no se publica.")
    topes = [f for f in filas if f["final"] == "tope_turnos"]
    if topes:
        muere(f"{len(topes)} partidas llegaron al tope de turnos sin terminar")

    meta = {
        "gauntlet": f"arena:campo={pathlib.Path(args.campo).name}@{hash_config(args.campo)}",
        "empezada_en": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t0)),
        # `paralelo` es uno de los campos que invalidan una comparacion en compara.py. Aqui
        # las dos ramas salen de la MISMA corrida, asi que siempre coincide; se guarda de
        # todas formas para que el reporte diga con cuantos hilos se jugo.
        "topologia": {"modo": "arena", "paralelo": args.hilos or os.cpu_count(),
                      "nucleos": os.cpu_count(), "hilos_por_nucleo": 1,
                      "nodos": args.nodos},
        "rng_version": "xoshiro256++/repo",
        "commit": commit_actual(),
        "version": "arena",
    }

    out = pathlib.Path(args.out)
    escribe(out / "a" / "torneo.sqlite", [f for f in filas if f["rama"] == "a"], meta,
            "rama-a", ha)
    escribe(out / "b" / "torneo.sqlite", [f for f in filas if f["rama"] == "b"], meta,
            "rama-b", hb)

    print(f"{len(filas)} partidas en {segundos:.1f}s "
          f"({len(filas) / segundos * 60:.0f}/min) -> {out}", file=sys.stderr)
    print(f"\ncompara con:\n  python3 training-room/compara.py "
          f"--a {out}/a/torneo.sqlite --b {out}/b/torneo.sqlite --slug rama-a,rama-b\n",
          file=sys.stderr)


if __name__ == "__main__":
    main()
