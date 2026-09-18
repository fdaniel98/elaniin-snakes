#!/usr/bin/env python3
"""Orquestador del Training Room: torneos en modo HTTP con el arbitro oficial.

    python3 training-room/tr.py match --gauntlet training-room/gauntlets/gauntlet-v1.json \
        --games 200 --out docs/results/torneo-<fecha>

El arbitro es el CLI oficial; nosotros no arbitramos nada. Aqui no hay reproducibilidad
bit a bit y no se promete: se persisten la semilla y el JSONL, que es lo unico que el
motor oficial permite (ver docs/decisions/ADR-0016-reproducibilidad-de-la-arena.md#d-0151).

Recursos justos: todas las snakes, la nuestra incluida, corren en contenedor con identicos
--cpus, --memory y --cpuset-cpus disjuntos. Sin pinning el p99 lo domina el throttling de
la cuota CFS y no el algoritmo, y entonces el torneo mide la maquina.

--dry-run imprime el plan completo y no ejecuta nada: sirve para revisar el reparto de
nucleos y la rotacion de asientos sin tener docker delante.
"""
import argparse
import hashlib
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
NUESTRO_SLUG = "v0-baseline"


def muere(mensaje):
    print(f"ERROR {mensaje}", file=sys.stderr)
    raise SystemExit(2)


def corre(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def docker_bin():
    for c in ("docker", "docker.exe"):
        if shutil.which(c) and corre([c, "version"]).returncode == 0:
            return c
    return None


def commit_actual():
    r = corre(["git", "-C", str(RAIZ), "rev-parse", "--short=12", "HEAD"])
    return r.stdout.strip() or "sin-git"


def hash_config(ruta):
    """El hash del config entra en cada partida: dos corridas con configs distintos no
    son la misma medicion aunque la snake se llame igual."""
    return hashlib.sha256(Path(ruta).read_bytes()).hexdigest()[:16]


# --------------------------------------------------------------- recursos justos
def reparte_nucleos(n_snakes, cpus_por_snake, fatal=True):
    """Devuelve (cpusets, motivo_del_no). Con fatal, un torneo que no cabe aborta.

    La regla es de §10.2 y no es negociable desde la linea de comandos: si el torneo no
    cabe, el numero que saldria no valdria, asi que se aborta en vez de avisar. En
    --dry-run se informa y se sigue, porque el plan se revisa en cualquier maquina y la
    que manda es donde se corra de verdad."""
    fisicos = os.cpu_count() or 0
    presupuesto = fisicos - 2
    total = n_snakes * cpus_por_snake
    motivo = None
    if cpus_por_snake != int(cpus_por_snake):
        motivo = "el pinning necesita cuotas enteras de CPU; una fraccionaria comparte nucleo"
    elif total > presupuesto:
        motivo = (
            f"no cabe: {n_snakes} snakes x {cpus_por_snake} CPU = {total} > {presupuesto} "
            f"(nucleos {fisicos} menos dos para el arbitro y el sistema). Baja --cpus del "
            "gauntlet o corre en una maquina mayor; no se mide asi."
        )
    if motivo and fatal:
        muere(motivo)
    paso = max(1, int(cpus_por_snake))
    cpusets = [",".join(str(n) for n in range(i * paso, i * paso + paso)) for i in range(n_snakes)]
    return cpusets, motivo


def topologia():
    hilos = "desconocido"
    try:
        for linea in corre(["lscpu"]).stdout.splitlines():
            if linea.startswith("Thread(s) per core"):
                hilos = linea.split(":")[1].strip()
    except Exception:
        pass
    gob = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
    return {
        "nucleos": os.cpu_count() or 0,
        "hilos_por_nucleo": hilos,
        # WSL2 no expone cpufreq: la frecuencia la gobierna el anfitrion y no se puede
        # leer ni fijar. Se registra como desconocido en vez de inventar un valor.
        "gobernador": gob.read_text().strip() if gob.exists() else "desconocido",
    }


# --------------------------------------------------------------- sqlite
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
  PRIMARY KEY (partida_id, slug));
"""


def abre_db(ruta):
    db = sqlite3.connect(ruta)
    db.executescript(ESQUEMA)
    return db


# --------------------------------------------------------------- match
def cmd_match(args):
    gauntlet = json.loads(Path(args.gauntlet).read_text())
    rivales = gauntlet["rivales"]
    composiciones = gauntlet["composiciones"]
    recursos = gauntlet["recursos"]
    partida = gauntlet["partida"]
    cpus = recursos["cpus"]

    jugadores = 1 + len(composiciones[0])
    cpusets, motivo_no_cabe = reparte_nucleos(jugadores, cpus, fatal=not args.dry_run)
    topo = topologia()

    salida = Path(args.out)
    salida.mkdir(parents=True, exist_ok=True)

    print(f"gauntlet:      {gauntlet['nombre']} (congelado {gauntlet['congelado']})")
    print(f"composiciones: {len(composiciones)}")
    print(f"maquina:       {topo['nucleos']} nucleos, {topo['hilos_por_nucleo']} hilos/nucleo, "
          f"governor {topo['gobernador']}")
    print(f"reparto:       {cpus} CPU y {recursos['memoria']} por snake; cpuset {cpusets}")
    if motivo_no_cabe:
        print(f"NO CABE aqui:  {motivo_no_cabe}")
        print("               (en seco se sigue; una corrida real abortaria)")
    print(f"partidas:      {args.games}")

    docker = docker_bin()
    if docker is None and not args.dry_run:
        muere("sin docker utilizable: el torneo necesita contenedores para que los recursos sean comparables")

    # El digest congelado es el contrato del campo: si la imagen local no es esa, lo que
    # se mediria no es este gauntlet. Aborta, no avisa.
    if not args.dry_run:
        for imagen, digest_esperado in gauntlet["imagenes"].items():
            r = corre([docker, "image", "inspect", "--format", "{{.Id}}", imagen])
            if r.returncode != 0:
                muere(f"falta la imagen {imagen}; corre './scripts/zoo.sh build <slug>'")
            real = r.stdout.strip()
            if not real.startswith(digest_esperado):
                muere(
                    f"{imagen} tiene digest {real[:19]} y {gauntlet['nombre']} congelo "
                    f"{digest_esperado}. Un campo distinto es otro gauntlet: crea gauntlet-v2."
                )
        print("OK   digests coinciden con el campo congelado")

    db = abre_db(salida / "torneo.sqlite")
    nuestro_commit = commit_actual()
    nuestro_hash = hash_config(RAIZ / "snake/config/default.json")

    plan = []
    for g in range(args.games):
        comp = composiciones[g % len(composiciones)]
        # Rotacion de asientos: la unidad de analisis es el bloque (una semilla por todas
        # las rotaciones), no la partida. Sin rotar, el asiento se confunde con la snake.
        asiento = g % jugadores
        semilla = args.seed_base + (g // jugadores)
        plan.append({"id": f"g{g:05d}", "comp": comp, "asiento": asiento, "semilla": semilla})

    if args.dry_run:
        print("\n-- plan (primeras 8 partidas) --")
        for p in plan[:8]:
            print(f"  {p['id']}  semilla={p['semilla']}  asiento_nuestro={p['asiento']}  "
                  f"rivales={','.join(p['comp'])}")
        bloques = len({p["semilla"] for p in plan})
        print(f"  ... {len(plan)} partidas en {bloques} bloques de {jugadores} rotaciones")
        print("\nDRY no se ejecuta nada")
        return 0

    muere("la ejecucion real del torneo llega en el siguiente commit; hoy solo --dry-run")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("match", help="torneo contra un campo congelado")
    m.add_argument("--gauntlet", required=True)
    m.add_argument("--games", type=int, required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--seed-base", type=int, default=1)
    m.add_argument("--dry-run", action="store_true")
    m.set_defaults(func=cmd_match)
    args = ap.parse_args()
    raise SystemExit(args.func(args))


if __name__ == "__main__":
    main()
