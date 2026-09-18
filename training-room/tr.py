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
import fcntl
import hashlib
import json
import os
import re
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



# --------------------------------------------------------------- exclusion mutua
def toma_el_cerrojo():
    """Un torneo a la vez por maquina, y no es una precaucion teorica.

    Dos corridas simultaneas usan los mismos nombres de contenedor, los mismos puertos y
    el mismo --out: cada una tumba los contenedores de la otra en su `finally` y las dos
    escriben el mismo JSONL. Paso de verdad, y dejo 199 de 200 partidas con el arbitro en
    error y archivos de cero bytes, con el arbitro jugando correctamente todo el rato.

    El cerrojo es global, no del directorio de salida: dos torneos con --out distintos
    chocarian igual, porque lo que comparten son los contenedores y los puertos."""
    ruta = RAIZ / ".tr.lock"
    fh = open(ruta, "w")
    try:
        fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        muere(
            f"ya hay un torneo corriendo en esta maquina ({ruta} esta tomado). Dos a la vez "
            "se pisan los contenedores y los puertos, y el resultado no vale. Espera a que "
            "termine, o matalo antes de lanzar otro."
        )
    fh.write(f"{os.getpid()}\n")
    fh.flush()
    return fh                      # se devuelve para que el cerrojo viva lo que el proceso


# --------------------------------------------------------------- contenedores
def imagen_nuestra(commit, hash_cfg):
    # La etiqueta lleva el hash del config: dos versiones de estrategia son binarios
    # distintos aunque el commit sea el mismo, y confundirlas seria medir una creyendo
    # medir la otra.
    return f"battlesnake/ours:{commit}-{hash_cfg}"


def construye_la_nuestra(docker, commit, hash_cfg, config, seco):
    """Nuestra snake corre en contenedor como las demas. Compararla fuera seria regalarle
    la maquina entera mientras los rivales viven con una cuota."""
    img = imagen_nuestra(commit, hash_cfg)
    if seco:
        print(f"DRY docker build -f deploy/Dockerfile -t {img} .")
        return img
    if corre([docker, "image", "inspect", img]).returncode == 0:
        print(f"OK   {img} ya existe")
        return img
    print(f"-- construyendo {img} (compila el proyecto entero, unos minutos) --")
    # El Dockerfile copia snake/config/default.json. Para medir OTRA estrategia se copia
    # ese config al sitio durante el build y se restaura despues: asi la imagen lleva
    # dentro exactamente el config cuyo hash se acaba de etiquetar, y no hay forma de
    # correr un torneo creyendo medir una version y estar midiendo la otra.
    destino = RAIZ / "snake/config/default.json"
    original = destino.read_bytes()
    cambiado = Path(config) != destino
    try:
        if cambiado:
            destino.write_bytes(Path(config).read_bytes())
        r = subprocess.run([docker, "build", "-f", "deploy/Dockerfile", "-t", img, "."],
                           cwd=RAIZ)
    finally:
        if cambiado:
            destino.write_bytes(original)
    if r.returncode != 0:
        muere("fallo el docker build de nuestra snake")
    return img


BANDERAS_AISLAMIENTO = [
    "--user", "65534:65534", "--read-only", "--tmpfs", "/tmp",
    "--cap-drop=ALL", "--security-opt=no-new-privileges", "--pids-limit=256",
]


def arranca_la_nuestra(docker, img, puerto, cpus, cpuset, memoria, seco):
    cmd = [docker, "run", "-d", "--name", "tr-ours", *BANDERAS_AISLAMIENTO,
           "--cpus", str(cpus), "--cpuset-cpus", cpuset, "--memory", memoria,
           "-p", f"127.0.0.1:{puerto}:8080", "-e", "PORT=8080", img]
    if seco:
        print("DRY " + " ".join(cmd))
        return f"http://127.0.0.1:{puerto}"
    corre([docker, "rm", "-f", "tr-ours"])
    if corre(cmd).returncode != 0:
        muere("no arranco el contenedor de nuestra snake")
    return f"http://127.0.0.1:{puerto}"


def espera(url, segundos=60):
    fin = time.time() + segundos
    while time.time() < fin:
        if corre(["curl", "-fsS", url]).returncode == 0:
            return True
        time.sleep(0.5)
    return False


def arbitro():
    ruta = shutil.which("battlesnake")
    if ruta:
        return ruta
    gopath = corre(["go", "env", "GOPATH"]).stdout.strip()
    candidato = Path(gopath) / "bin" / "battlesnake" if gopath else None
    if candidato and candidato.exists():
        return str(candidato)
    muere("no encuentro el CLI oficial; compilalo con el SHA de docs/SOURCES.md")


# --------------------------------------------------------------- lectura del JSONL
def lee_partida(jsonl, log_arbitro):
    """Deriva de un JSONL lo que el arbitro SI exporta, y nada mas.

    El JSONL no trae puestos: la serpiente eliminada desaparece de `board.snakes` y el
    ultimo turno no se exporta (ver docs/rules.md#r-12). Lo que si hay es el ultimo turno
    en que cada una aparecio, y una linea final con el ganador. De ahi salen los puestos,
    con rango compartido promediado para las que caen en el mismo turno, que es la
    convencion propia de placements() y no una regla del motor oficial.

    **Nada de lo que entra aqui es de fiar.** El JSONL lo escribe un binario de Go que
    puede cortarse a media linea si se mata el proceso, y el fuzz de `scripts/fuzz_tr.py`
    lo demostro: la primera version lanzaba `AttributeError` en el estado 7 de 500. Una
    excepcion aqui aborta un torneo de cinco horas por un fichero roto. Asi que una linea
    ilegible se salta, un tipo inesperado se ignora, y si no queda nada utilizable se
    devuelve None, que el llamante ya sabe registrar."""
    try:
        lineas = [l for l in Path(jsonl).read_text(encoding="utf-8", errors="replace").splitlines()
                  if l.strip()]
    except OSError:
        return None
    if len(lineas) < 3:
        return None
    try:
        final = json.loads(lineas[-1])
    except ValueError:
        final = {}
    if not isinstance(final, dict):
        final = {}

    ultimo_turno, latencias, nombres = {}, {}, {}
    turnos = 0
    for linea in lineas[1:-1]:
        try:
            estado = json.loads(linea)
        except ValueError:
            continue
        if not isinstance(estado, dict):
            continue
        turno = estado.get("turn", 0)
        if not isinstance(turno, int):
            continue
        turnos = max(turnos, turno)
        board = estado.get("board")
        if not isinstance(board, dict):
            continue
        serpientes = board.get("snakes")
        if not isinstance(serpientes, list):
            continue
        for s in serpientes:
            if not isinstance(s, dict):
                continue
            sid = s.get("id")
            if not isinstance(sid, str):
                continue
            ultimo_turno[sid] = turno
            nombre = s.get("name")
            nombres[sid] = nombre if isinstance(nombre, str) else ""
            if turno == 0:
                continue
            try:
                latencias.setdefault(sid, []).append(float(s.get("latency", "")))
            except (TypeError, ValueError):
                pass
    if not ultimo_turno:
        return None

    # Puestos: mas turnos sobrevividos, mejor puesto. Empates a rango promediado.
    ganador = final.get("winnerId")
    if not isinstance(ganador, str):
        ganador = None
    if ganador and ganador in ultimo_turno:
        # Solo si el ganador aparecio en algun turno: un id inventado en la ultima linea
        # metia una serpiente fantasma y descuadraba el reparto de puestos.
        ultimo_turno[ganador] = max(ultimo_turno.values()) + 1
    orden = sorted(ultimo_turno.items(), key=lambda kv: -kv[1])
    puestos, i = {}, 0
    while i < len(orden):
        j = i
        while j + 1 < len(orden) and orden[j + 1][1] == orden[i][1]:
            j += 1
        promedio = sum(range(i + 1, j + 2)) / (j - i + 1)
        for k in range(i, j + 1):
            puestos[orden[k][0]] = promedio
        i = j + 1

    # Que id es cada url, del log del arbitro: casar por nombre se rompe al renombrar.
    por_url = {}
    try:
        texto = Path(log_arbitro).read_text(encoding="utf-8", errors="replace")
    except OSError:
        texto = ""
    import re
    for sid, url in re.findall(r"Snake ID:\s+(\S+)\s+URL:\s+(\S+?),", texto):
        por_url[url.rstrip("/")] = sid
    return {"turnos": turnos, "puestos": puestos, "latencias": latencias,
            "nombres": nombres, "por_url": por_url, "empate": bool(final.get("isDraw")),
            "ganador": ganador, "ultimo_turno": ultimo_turno}


# El arbitro escribe un fallo en DOS lineas: la cabecera `Request to <url>/<ruta> failed`
# y, debajo, `\tError: Post "<url>/<ruta>": <motivo>`. Las dos llevan la url dentro, asi
# que contar "lineas que mencionan la url y hablan de un motivo" contaba cada fallo dos
# veces: el torneo publico 8 timeouts nuestros cuando eran 4, y 192 de Devin cuando eran
# 96. Ahora se cuenta UNA incidencia por cabecera y el motivo se lee de la linea siguiente.
CABECERA = re.compile(r"Request to (\S+?)/(move|start|end) failed")
NON_OK = re.compile(r"Got non-ok status code from (\S+?)/(move|start|end)")
MOTIVOS = (
    ("timeout", "context deadline exceeded"),
    ("json", "Failed to decode JSON"),
    ("json", "Failed to parse response"),
    ("json", "Failed to read response body"),
    ("movimiento", "invalid move"),
)


def incidencias_de(reflog, url):
    """Quejas del arbitro sobre `url`, una por peticion fallida, clasificadas por motivo.

    `/end` se excluye a proposito: el ganador se fija ANTES de enviarlo
    (`cli/commands/play.go:314-319`), asi que fallar ahi no cuesta ni un movimiento, y hay
    rivales que no responden a `/end` nunca -Eremetic Eric fallo las 198 veces del torneo-."""
    cuenta = {"timeout": 0, "status": 0, "json": 0, "movimiento": 0, "conexion": 0}
    ruta_log = Path(reflog)
    if not ruta_log.exists():
        return cuenta
    try:
        lineas = ruta_log.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return cuenta
    for i, linea in enumerate(lineas):
        m = CABECERA.search(linea)
        if m:
            if m.group(1) != url or m.group(2) == "end":
                continue
            motivo = lineas[i + 1] if i + 1 < len(lineas) else ""
            etiqueta = "conexion"
            for nombre, patron in MOTIVOS:
                if patron in motivo:
                    etiqueta = nombre
                    break
            cuenta[etiqueta] += 1
            continue
        m = NON_OK.search(linea)
        if m and m.group(1) == url and m.group(2) != "end":
            cuenta["status"] += 1
    return cuenta


def percentil(valores, p):
    if not valores:
        return None
    o = sorted(valores)
    return o[min(len(o) - 1, int(len(o) * p))]


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
    ruta_config = Path(args.config) if args.config else RAIZ / "snake/config/default.json"
    if not ruta_config.exists():
        muere(f"no existe el config {ruta_config}")
    nuestro_hash = hash_config(ruta_config)
    print(f"config:        {ruta_config} (hash {nuestro_hash})")

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

    # ---------------------------------------------------------- arranque
    cerrojo = toma_el_cerrojo()    # noqa: F841 - vive hasta que el proceso muere
    docker = docker_bin()
    commit = nuestro_commit
    img_nuestra = construye_la_nuestra(docker, commit, nuestro_hash, ruta_config, seco=False)

    # Los contenedores se levantan UNA vez para todo el torneo, no por partida: un
    # servidor de Battlesnake es apatrida entre partidas -recibe /start cada vez- y
    # reiniciar cuatro contenedores 200 veces mediria el arranque de docker, no las
    # snakes. El arranque en frio se mide aparte, en el soak (ver docs/performance.md#p-07).
    puerto_base = 9700
    urls = {}
    slugs = composiciones[0]
    try:
        urls[NUESTRO_SLUG] = arranca_la_nuestra(
            docker, img_nuestra, puerto_base, cpus, cpusets[0], recursos["memoria"], seco=False)
        for i, slug in enumerate(slugs):
            r = corre([str(RAIZ / "scripts/zoo.sh"), "up", slug,
                       "--port", str(puerto_base + 1 + i), "--cpus", str(cpus),
                       "--cpuset", cpusets[i + 1], "--memory", recursos["memoria"]],
                      cwd=RAIZ)
            if r.returncode != 0:
                muere(f"no arranco {slug}: {r.stderr.strip()[:300]}")
            urls[slug] = r.stdout.strip()
        for slug, url in urls.items():
            if not espera(url):
                muere(f"{slug} no respondio en {url}")
        print("OK   las cuatro snakes responden")

        cli = arbitro()
        # Nada de borrar lo anterior: una corrida de cinco horas que empieza tirando el
        # resultado de la anterior es una forma cara de perder datos. Las partidas ya
        # jugadas con el arbitro en 0 se saltan, asi que una corrida interrumpida se
        # reanuda sola con el mismo --out.
        ya_jugadas = {fila[0] for fila in db.execute(
            "SELECT id FROM partidas WHERE arbitro_rc = 0")}
        if ya_jugadas:
            print(f"OK   {len(ya_jugadas)} partidas ya estaban jugadas; se reanuda")
        fallos = 0
        empezado = time.time()
        for p in plan:
            if p["id"] in ya_jugadas:
                print("=", end="", flush=True)
                continue
            # La rotacion se implementa reordenando los argumentos del arbitro: el orden
            # de --name/--url ES el asiento. No hace falta reiniciar nada.
            orden = [NUESTRO_SLUG] + list(p["comp"])
            orden = orden[-p["asiento"]:] + orden[:-p["asiento"]] if p["asiento"] else orden
            argumentos = []
            for slug in orden:
                argumentos += ["--name", slug, "--url", urls[slug]]
            jsonl = salida / f"{p['id']}.jsonl"
            reflog = salida / f"{p['id']}.ref.log"
            with open(reflog, "w") as errores:
                rc = subprocess.run(
                    [cli, "play", "-W", str(partida["ancho"]), "-H", str(partida["alto"]),
                     "-g", partida["ruleset"], "-m", partida["mapa"],
                     "-t", str(partida["timeout_ms"]), "-r", str(p["semilla"]),
                     *argumentos, "-o", str(jsonl)],
                    cwd=RAIZ, stdout=subprocess.DEVNULL, stderr=errores).returncode
            if rc != 0:
                fallos += 1
            guarda(db, gauntlet, p, jsonl, reflog, rc, urls, orden, topo, commit,
                   nuestro_hash, img_nuestra, gauntlet["imagenes"])
            # Commit por partida, no al final: lo que ya se jugo no se pierde porque la
            # 190 falle, y `sqlite3` desde otra terminal puede mirar el avance.
            db.commit()
            print(".", end="", flush=True)
            if (plan.index(p) + 1) % 50 == 0:
                print(f" {plan.index(p) + 1}", flush=True)
        print()
    finally:
        corre([docker, "rm", "-f", "tr-ours"])
        corre([str(RAIZ / "scripts/zoo.sh"), "down", "--all"], cwd=RAIZ)

    minutos = (time.time() - empezado) / 60
    print(f"jugadas {len(plan)} partidas en {minutos:.1f} min "
          f"({len(plan) / max(minutos, 1e-9):.1f} partidas/min)")
    print(f"resultados en {salida}/torneo.sqlite")
    if fallos:
        print(f"FAIL {fallos} partidas con el arbitro en error", file=sys.stderr)
        return 1
    return 0


def guarda(db, gauntlet, p, jsonl, reflog, rc, urls, orden, topo, commit, hash_cfg,
           img_nuestra, imagenes):
    """`orden` es la lista de slugs en el orden en que se le pasaron al arbitro, que ES el
    asiento. Se guarda para todas las snakes, no solo la nuestra: si un asiento favorece,
    se ve en los rivales igual que en nosotros, y eso es lo que la rotacion anula."""
    datos = lee_partida(jsonl, reflog)
    if datos is None:
        db.execute("INSERT OR REPLACE INTO partidas VALUES (?,?,?,?,?,?,?,?,?,?)",
                   (p["id"], gauntlet["nombre"], p["semilla"], str(jsonl), p["asiento"],
                    None, rc, time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                    json.dumps(topo), "arbitro-oficial"))
        return
    db.execute("INSERT OR REPLACE INTO partidas VALUES (?,?,?,?,?,?,?,?,?,?)",
               (p["id"], gauntlet["nombre"], p["semilla"], str(jsonl), p["asiento"],
                datos["turnos"], rc, time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                json.dumps(topo), "arbitro-oficial"))
    id_por_slug = {slug: datos["por_url"].get(url.rstrip("/")) for slug, url in urls.items()}
    faltan = [slug for slug, sid in id_por_slug.items() if sid is None]
    if faltan:
        # Sin el id no se puede casar nada de esa snake. Se registra y no se inventa.
        print(f"\nAVISO {p['id']}: el log del arbitro no da el id de {', '.join(faltan)}",
              file=sys.stderr)
    for slug, sid in id_por_slug.items():
        if sid is None:
            continue
        nuestra = slug == NUESTRO_SLUG
        db.execute("INSERT OR REPLACE INTO participantes VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                   (p["id"], slug, datos["nombres"].get(sid, slug),
                    "v0" if nuestra else "zoo", commit if nuestra else None,
                    hash_cfg if nuestra else None,
                    img_nuestra if nuestra else list(imagenes)[0],
                    orden.index(slug),
                    datos["puestos"].get(sid), datos["ultimo_turno"].get(sid),
                    None))
        lat = datos["latencias"].get(sid, [])
        inc = incidencias_de(reflog, urls[slug])
        db.execute("INSERT OR REPLACE INTO latencias VALUES (?,?,?,?,?,?,?,?)",
                   (p["id"], slug, percentil(lat, 0.50), percentil(lat, 0.95),
                    percentil(lat, 0.99), max(lat) if lat else None,
                    inc["timeout"], len(lat)))


# --------------------------------------------------------------- reanaliza
def cmd_reanaliza(args):
    """Recalcula lo derivado de una corrida ya jugada, sin volver a jugar ni tocar docker.

    Existe porque una metrica que falto la primera vez no puede costar cinco horas de
    torneo: los JSONL y los logs del arbitro estan en disco, y todo lo que este programa
    calcula sale de ahi.

    Informa de FILAS MODIFICADAS, no de vueltas del bucle. La primera version contaba
    iteraciones y dijo "recalculadas 800 filas" sin haber tocado ninguna, porque el
    emparejamiento de grupos de la expresion regular estaba cruzado y ningun `WHERE`
    casaba. Un contador que no puede quedarse en cero no mide nada."""
    import re

    salida = Path(args.out)
    db = abre_db(salida / "torneo.sqlite")
    # `Snake ID: <uuid> URL: <url>, Name: "<nombre>"`, tal cual lo imprime el arbitro al
    # arrancar cada partida. El `slug` de la base es el --name que le pasamos, o sea el
    # tercer grupo; la url con la que se buscan las quejas es el segundo.
    LINEA = re.compile(r'Snake ID:\s+(\S+)\s+URL:\s+(\S+?), Name: "([^"]+)"')

    modificadas = 0
    sin_log = 0
    for (pid,) in db.execute("SELECT id FROM partidas").fetchall():
        reflog = salida / f"{pid}.ref.log"
        if not reflog.exists():
            sin_log += 1
            continue
        texto = reflog.read_text(encoding="utf-8", errors="replace")
        for _sid, url, nombre in LINEA.findall(texto):
            inc = incidencias_de(reflog, url)
            cur = db.execute(
                "UPDATE latencias SET timeouts = ? WHERE partida_id = ? AND slug = ?",
                (inc["timeout"], pid, nombre))
            modificadas += cur.rowcount
    db.commit()

    print(f"filas de latencias modificadas: {modificadas}")
    if sin_log:
        print(f"AVISO {sin_log} partidas sin log del arbitro: no se pudo recalcular")
    if modificadas == 0:
        print("FAIL no se modifico ninguna fila: el recalculo no hizo nada", file=sys.stderr)
        return 1
    for fila in db.execute("""SELECT l.slug, SUM(l.timeouts), SUM(l.movimientos)
                              FROM latencias l JOIN partidas g ON g.id = l.partida_id
                              WHERE g.arbitro_rc = 0 GROUP BY l.slug ORDER BY 2 DESC"""):
        print("   %-16s timeouts=%-5s movimientos=%s" % fila)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("match", help="torneo contra un campo congelado")
    m.add_argument("--gauntlet", required=True)
    m.add_argument("--games", type=int, required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--seed-base", type=int, default=1)
    m.add_argument("--config", default=None,
                   help="config de estrategia a meter en la imagen; por defecto el del repo")
    m.add_argument("--dry-run", action="store_true")
    m.set_defaults(func=cmd_match)
    r = sub.add_parser("reanaliza", help="recalcula lo derivado de una corrida ya jugada")
    r.add_argument("--out", required=True)
    r.set_defaults(func=cmd_reanaliza)
    args = ap.parse_args()
    raise SystemExit(args.func(args))


if __name__ == "__main__":
    main()
