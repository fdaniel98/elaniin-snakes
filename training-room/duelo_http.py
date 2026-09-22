#!/usr/bin/env python3
"""Duelos 1v1 ESTANDAR por HTTP contra una snake del zoo, con el arbitro oficial.

    python3 training-room/duelo_http.py --config snake/config/default.json \\
        --rival snork-tree --semillas 20 --out docs/results/duelo-snork-v5

    # A/B: la misma orden con otra config y el mismo --semilla-base, y luego
    python3 training-room/compara.py --a docs/results/duelo-snork-v5/torneo.sqlite \\
        --b docs/results/duelo-snork-v13/torneo.sqlite

Por que existe: el desempate del torneo es un 1v1 estandar, y en la arena solo se puede
medir contra copias nuestras. En espejo, una candidata que explota a un rival concreto no
tiene a quien explotar (ver docs/experimentos-instrumento.md#s-territorio-duelo-r2). Aqui el
rival es codigo ajeno, en su contenedor aislado, y el arbitro es el CLI oficial.

Cada **bloque** es una semilla jugada desde los dos asientos, igual que en la arena, y la
base se escribe con el mismo esquema que `arena_ab.py`, asi que `compara.py` es la unica
ruta estadistica. Lo que NO es: reproducible bit a bit. El rival y nuestro servidor piensan
contra el RELOJ, asi que la carga de la maquina entra en el resultado; por eso se corre en
serie y el reporte guarda la topologia.

`--rival-url` salta el contenedor y juega contra una URL ya levantada: sirve para probar el
script en una maquina sin docker, no para publicar numeros.
"""

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import sqlite3
import statistics
import subprocess
import sys
import tempfile
import time
import urllib.request

RAIZ = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "training-room"))
from arena_ab import ESQUEMA, hash_config, commit_actual  # noqa: E402


def muere(msg):
    print(f"ERROR {msg}", file=sys.stderr)
    raise SystemExit(2)


def responde(url, segundos=60):
    fin = time.time() + segundos
    while time.time() < fin:
        try:
            with urllib.request.urlopen(url, timeout=2) as r:
                if r.status == 200:
                    return True
        except Exception:
            time.sleep(0.5)
    return False


def cli():
    c = shutil.which("battlesnake")
    if not c:
        gopath = subprocess.run(["go", "env", "GOPATH"], capture_output=True, text=True).stdout.strip() \
            if shutil.which("go") else ""
        cand = pathlib.Path(gopath) / "bin" / "battlesnake" if gopath else None
        c = str(cand) if cand and cand.exists() else None
    if not c:
        muere("falta el CLI oficial `battlesnake`: corre ./scripts/zoo-game.sh una vez, que lo compila")
    return c


def campo_manifest(slug, campo):
    m = RAIZ / "zoo" / "manifests" / f"{slug}.toml"
    if not m.exists():
        muere(f"no existe {m}")
    for linea in m.read_text().splitlines():
        if linea.split("=")[0].strip() == campo:
            return linea.split("=", 1)[1].strip().strip('"')
    return ""


def analiza(jsonl, nuestra, rival, timeout):
    lineas = [json.loads(l) for l in pathlib.Path(jsonl).read_text().splitlines() if l.strip()]
    estados = [l for l in lineas if "board" in l]
    fin = next((l for l in reversed(lineas) if "winnerName" in l or "isDraw" in l), {})
    turnos = estados[-1]["turn"] if estados else 0

    def ultimo_turno(nombre):
        t = -1
        for e in estados:
            if any(s["name"] == nombre for s in e["board"]["snakes"]):
                t = e["turn"]
        return t

    if fin.get("isDraw"):
        puesto = 1.5
    elif fin.get("winnerName") == nuestra:
        puesto = 1.0
    elif fin.get("winnerName") == rival:
        puesto = 2.0
    else:  # sin linea de resultado: se decide por quien aguanto mas
        a, b = ultimo_turno(nuestra), ultimo_turno(rival)
        puesto = 1.0 if a > b else 2.0 if b > a else 1.5

    def latencias(nombre):
        vals, faltan = [], 0
        for e in estados[1:]:
            for s in e["board"]["snakes"]:
                if s["name"] == nombre:
                    try:
                        vals.append(int(s.get("latency") or ""))
                    except ValueError:
                        faltan += 1
        vals.sort()
        q = lambda p: vals[min(len(vals) - 1, int(round(p * (len(vals) - 1))))] if vals else None
        return {"p50": q(.5), "p95": q(.95), "p99": q(.99), "max": vals[-1] if vals else None,
                "timeouts": sum(1 for v in vals if v >= timeout) + faltan, "movimientos": len(vals)}

    causa = "viva" if puesto == 1.0 else ("empate" if puesto == 1.5 else "derrota")
    return {"puesto": puesto, "turnos": turnos, "turnos_vividos": ultimo_turno(nuestra),
            "causa": causa, "lat_nuestra": latencias(nuestra), "lat_rival": latencias(rival)}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--config", required=True, help="config de NUESTRA snake")
    ap.add_argument("--rival", default="snork-tree", help="slug del zoo")
    ap.add_argument("--rival-url", help="URL de un rival ya levantado (sin docker)")
    ap.add_argument("--semillas", type=int, default=20, help="bloques: cada uno son 2 partidas")
    ap.add_argument("--semilla-base", type=int, default=1)
    ap.add_argument("--timeout", type=int, default=500)
    ap.add_argument("--puerto", type=int, default=8120)
    ap.add_argument("--puerto-rival", type=int, default=8121)
    ap.add_argument("--cpus", help="--cpus del contenedor rival (recursos justos)")
    ap.add_argument("--cpuset", help="--cpuset-cpus del contenedor rival")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    config = pathlib.Path(args.config).resolve()
    if not config.exists():
        muere(f"no existe {config}")
    binario = RAIZ / "build/release/bin/battlesnake-server"
    if not binario.exists():
        muere(f"falta {binario}; corre 'cmake --build --preset release'")
    # Mismo agujero que cerro arena_torneo: un binario viejo ignora claves nuevas en
    # silencio. ver docs/experimentos-instrumento.md#s-territorio-duelo-r
    fuera = subprocess.run([str(RAIZ / "build/release/bin/arena_torneo"), "--a", str(config),
                            "--campo", str(config), "--bloques", "0"],
                           capture_output=True, text=True)
    if fuera.returncode == 2 and "no conoce" in fuera.stderr:
        muere(fuera.stderr.strip())
    arbitro = cli()

    out = pathlib.Path(args.out)
    (out / "partidas").mkdir(parents=True, exist_ok=True)
    procesos = []

    def limpia():
        for p in procesos:
            p.terminate()
        if not args.rival_url:
            subprocess.run([str(RAIZ / "scripts/zoo.sh"), "down", args.rival],
                           capture_output=True)

    try:
        # Nuestro servidor lee snake/config/default.json RELATIVO al directorio de trabajo:
        # se le monta uno temporal con la config pedida y nada mas.
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="duelo-"))
        (tmp / "snake/config").mkdir(parents=True)
        shutil.copy(config, tmp / "snake/config/default.json")
        log = open(out / "servidor.log", "w")
        procesos.append(subprocess.Popen([str(binario)], cwd=tmp, stdout=log, stderr=log,
                                         env={**os.environ, "PORT": str(args.puerto)}))
        url_nuestra = f"http://127.0.0.1:{args.puerto}"
        if not responde(url_nuestra + "/health", 20):
            muere("nuestro servidor no respondio en /health")

        if args.rival_url:
            url_rival = args.rival_url.rstrip("/")
        else:
            up = [str(RAIZ / "scripts/zoo.sh"), "up", args.rival, "--port", str(args.puerto_rival)]
            if args.cpus:
                up += ["--cpus", args.cpus]
            if args.cpuset:
                up += ["--cpuset", args.cpuset]
            r = subprocess.run(up, capture_output=True, text=True)
            if r.returncode != 0:
                muere(f"no arranco {args.rival}: {r.stderr.strip()}  "
                      f"(¿falta './scripts/zoo.sh build {args.rival}'?)")
            ep = campo_manifest(args.rival, "entrypoint")
            url_rival = f"http://127.0.0.1:{args.puerto_rival}" + (f"/{ep}" if ep else "")
        if not responde(url_rival, 60):
            muere(f"el rival no respondio en {url_rival}")
        print(f"nuestra: {url_nuestra} ({config.name})   rival: {url_rival}", file=sys.stderr)

        filas, t0 = [], time.time()
        total = args.semillas * 2
        for b in range(args.semillas):
            semilla = args.semilla_base + b
            for asiento in (0, 1):
                jsonl = out / "partidas" / f"s{semilla:05d}-a{asiento}.jsonl"
                orden = [("nuestra", url_nuestra), ("rival", url_rival)]
                if asiento == 1:
                    orden.reverse()
                cmd = [arbitro, "play", "-W", "11", "-H", "11", "-g", "standard", "-m", "standard",
                       "-t", str(args.timeout), "-r", str(semilla), "-o", str(jsonl)]
                for nombre, url in orden:
                    cmd += ["-n", nombre, "-u", url]
                rc = subprocess.run(cmd, capture_output=True, text=True).returncode
                a = analiza(jsonl, "nuestra", "rival", args.timeout)
                a.update({"bloque": b, "semilla": semilla, "asiento": asiento, "rc": rc,
                          "jsonl": str(jsonl.relative_to(out))})
                filas.append(a)
                hechas = len(filas)
                resta = (time.time() - t0) / hechas * (total - hechas) / 60
                gan = sum(1 for f in filas if f["puesto"] == 1.0)
                print(f"\r{hechas}/{total} partidas  ganadas {gan}  ~{resta:.0f} min restantes ",
                      end="", file=sys.stderr, flush=True)
        print(file=sys.stderr)
    finally:
        limpia()

    imagen = f"zoo/{args.rival}@" + campo_manifest(args.rival, "sha")[:12] if not args.rival_url \
        else f"zoo/url:{args.rival_url}"
    topologia = {"modo": "http-1v1-standard", "rival": args.rival, "paralelo": 1,
                 "nucleos": os.cpu_count(), "hilos_por_nucleo": 1,
                 "cpus_rival": args.cpus or "", "cpuset_rival": args.cpuset or ""}
    db_path = out / "torneo.sqlite"
    if db_path.exists():
        db_path.unlink()
    db = sqlite3.connect(db_path)
    db.executescript(ESQUEMA)
    slug = config.stem
    hconf = hash_config(config)
    empezada = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t0))
    for f in filas:
        pid = f"{f['semilla']:05d}-{f['asiento']}"
        db.execute("INSERT INTO partidas VALUES (?,?,?,?,?,?,?,?,?,?)",
                   (pid, f"http:{imagen}", f["semilla"], f["jsonl"], f["asiento"], f["turnos"],
                    f["rc"], empezada, json.dumps(topologia, sort_keys=True), "cli"))
        db.execute("INSERT INTO participantes VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                   (pid, slug, slug, "http", commit_actual(), hconf, "local/battlesnake-server",
                    f["asiento"], f["puesto"], f["turnos_vividos"], f["causa"]))
        db.execute("INSERT INTO participantes VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                   (pid, args.rival, args.rival, "zoo", "", "", imagen, 1 - f["asiento"],
                    3.0 - f["puesto"], f["turnos"], ""))
        for quien, lat in ((slug, f["lat_nuestra"]), (args.rival, f["lat_rival"])):
            db.execute("INSERT INTO latencias (partida_id, slug, p50, p95, p99, maximo, timeouts, "
                       "movimientos, fallos_conexion) VALUES (?,?,?,?,?,?,?,?,?)",
                       (pid, quien, lat["p50"], lat["p95"], lat["p99"], lat["max"],
                        lat["timeouts"], lat["movimientos"], 0))
    db.commit()
    db.close()

    gan = sum(1 for f in filas if f["puesto"] == 1.0)
    emp = sum(1 for f in filas if f["puesto"] == 1.5)
    tn = sum(f["lat_nuestra"]["timeouts"] for f in filas)
    tr = sum(f["lat_rival"]["timeouts"] for f in filas)
    p99 = max((f["lat_nuestra"]["p99"] or 0) for f in filas)
    print(f"\n{config.name} contra {args.rival}: {gan} ganadas, {emp} empates, "
          f"{len(filas) - gan - emp} perdidas de {len(filas)} "
          f"(puesto medio {statistics.mean(f['puesto'] for f in filas):.3f})")
    print(f"timeouts: nuestra {tn}, rival {tr}   p99 de nuestra latencia (peor partida) {p99} ms")
    print(f"base: {db_path}")
    if tr:
        print("AVISO: el rival no contesto a tiempo alguna vez; esas partidas regalan puestos.")


if __name__ == "__main__":
    main()
