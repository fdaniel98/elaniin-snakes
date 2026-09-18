#!/usr/bin/env python3
"""Fuzz de payloads HTTP contra el servidor: la metrica `fuzz_states` del loop.

    python3 scripts/fuzz_server.py --estados 10000 [--puerto 8123] [--semilla 1]

Que garantiza, y por que hace falta:

`INV-12` dice que `/move` NUNCA devuelve 5xx y siempre devuelve una de las cuatro
direcciones. La fase 2 lo comprobo con 42 payloads adversos escritos a mano, que cubren lo
que a una persona se le ocurre. El umbral del loop pide 10 000 estados porque lo que tumba
un servidor no suele ser lo que se le ocurre a nadie: es la tercera combinacion de tres
mutaciones sobre un payload valido.

Los estados salen de MUTAR los fixtures, no de generar JSON al azar: un JSON aleatorio lo
rechaza el parser en la primera rama y no ejercita nada. Aqui se parte de algo que el
servidor acepta y se le rompe una pieza cada vez.

Determinista: misma semilla, mismos estados.
"""
import argparse
import json
import pathlib
import random
import subprocess
import sys
import time
import urllib.error
import urllib.request

RAIZ = pathlib.Path(__file__).resolve().parent.parent
DIRECCIONES = {"up", "down", "left", "right"}


def post(url, cuerpo, timeout=5.0):
    """Devuelve (status, texto). Un 5xx NO lanza: es justo lo que se quiere medir."""
    datos = cuerpo if isinstance(cuerpo, bytes) else json.dumps(cuerpo).encode()
    req = urllib.request.Request(url, data=datos,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


# --------------------------------------------------------------------- mutaciones
def rutas(doc, prefijo=()):
    """Todas las rutas de un documento JSON, para poder tocar una sola pieza."""
    yield prefijo, doc
    if isinstance(doc, dict):
        for k, v in doc.items():
            yield from rutas(v, prefijo + (k,))
    elif isinstance(doc, list):
        for i, v in enumerate(doc):
            yield from rutas(v, prefijo + (i,))


def pon(doc, ruta, valor):
    if not ruta:
        return valor
    nodo = doc
    for paso in ruta[:-1]:
        nodo = nodo[paso]
    nodo[ruta[-1]] = valor
    return doc


def quita(doc, ruta):
    if not ruta:
        return {}
    nodo = doc
    for paso in ruta[:-1]:
        nodo = nodo[paso]
    try:
        del nodo[ruta[-1]]
    except (KeyError, IndexError, TypeError):
        pass
    return doc


# Valores que rompen una pieza de un payload valido. El nulo y el emoji van por punto de
# codigo: un literal de control dentro del fuente es invisible al leerlo.
RAROS = [None, True, False, 0, -1, 1 << 40, -(1 << 40), 3.5, float("1e308"),
         "", "up", "UP", "arriba", chr(0), chr(0x1F600), [], {}, [[]], {"x": {}},
         "-1", "11", " 0 ", "0x0", "1e3"]


def muta(rng, base):
    """Una a tres mutaciones sobre una copia del fixture."""
    doc = json.loads(json.dumps(base))
    for _ in range(rng.randint(1, 3)):
        caminos = [r for r, _ in rutas(doc) if r]
        if not caminos:
            break
        ruta = rng.choice(caminos)
        cual = rng.random()
        if cual < 0.35:
            doc = pon(doc, ruta, rng.choice(RAROS))
        elif cual < 0.60:
            doc = quita(doc, ruta)
        elif cual < 0.80:
            # Duplicar o vaciar: longitudes y profundidades que el parser no espera.
            nodo = doc
            try:
                for paso in ruta[:-1]:
                    nodo = nodo[paso]
                valor = nodo[ruta[-1]]
                nodo[ruta[-1]] = [valor] * rng.choice([0, 2, 50])
            except (KeyError, IndexError, TypeError):
                pass
        else:
            doc = pon(doc, ruta, rng.choice([{"x": rng.randint(-50, 50),
                                              "y": rng.randint(-50, 50)}, [1, 2, 3]]))
    return doc


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--estados", type=int, default=10000)
    ap.add_argument("--puerto", type=int, default=8123)
    ap.add_argument("--semilla", type=int, default=1)
    ap.add_argument("--fixtures", default=str(RAIZ / "tests/fixtures"))
    ap.add_argument("--binario", default=str(RAIZ / "build/release/bin/battlesnake-server"))
    args = ap.parse_args()

    semillas = sorted(pathlib.Path(args.fixtures).glob("*.json"))
    if len(semillas) < 10:
        print(f"FAIL solo hay {len(semillas)} fixtures de partida", file=sys.stderr)
        return 1
    bases = [json.loads(p.read_text(encoding="utf-8")) for p in semillas]

    servidor = subprocess.Popen(
        [args.binario],
        env={"PORT": str(args.puerto), "PATH": "/usr/bin:/bin"},
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base_url = f"http://127.0.0.1:{args.puerto}"
    i = 0
    try:
        listo = False
        for _ in range(100):
            try:
                urllib.request.urlopen(base_url + "/health", timeout=1)
                listo = True
                break
            except Exception:
                time.sleep(0.05)
        if not listo:
            print("FAIL el servidor no arranco", file=sys.stderr)
            return 1

        rng = random.Random(args.semilla)
        fallos = []
        cuenta = {"5xx": 0, "movimiento_invalido": 0, "sin_respuesta": 0}
        rutas_http = ["/move", "/start", "/end"]
        empezado = time.perf_counter()
        for i in range(args.estados):
            doc = muta(rng, bases[i % len(bases)])
            ruta = rutas_http[i % len(rutas_http)]
            try:
                status, texto = post(base_url + ruta, doc)
            except Exception as exc:
                cuenta["sin_respuesta"] += 1
                fallos.append(f"estado {i} {ruta}: sin respuesta ({exc})")
                continue
            if status >= 500:
                cuenta["5xx"] += 1
                fallos.append(f"estado {i} {ruta}: HTTP {status}")
                continue
            if ruta == "/move":
                try:
                    movimiento = json.loads(texto).get("move")
                except ValueError:
                    movimiento = None
                if movimiento not in DIRECCIONES:
                    cuenta["movimiento_invalido"] += 1
                    fallos.append(f"estado {i}: /move devolvio {movimiento!r}")
            if len(fallos) > 20:
                break

        # El servidor tiene que seguir en pie DESPUES del fuzz: un proceso muerto no
        # devuelve 5xx, no devuelve nada, y eso se cuenta como el fallo que es.
        sigue_vivo = servidor.poll() is None
        if not sigue_vivo:
            fallos.append("el servidor murio durante el fuzz")

        segundos = time.perf_counter() - empezado
        resultado = {
            "fuzz_states": args.estados if not fallos else i + 1,
            "semilla": args.semilla,
            "fixtures_semilla": len(semillas),
            "http_5xx": cuenta["5xx"],
            "movimientos_invalidos": cuenta["movimiento_invalido"],
            "sin_respuesta": cuenta["sin_respuesta"],
            "servidor_vivo_al_final": sigue_vivo,
            "segundos": round(segundos, 1),
            "estados_por_segundo": round(args.estados / segundos, 1) if segundos else 0,
        }
        print(json.dumps(resultado, indent=2, ensure_ascii=False))
        for f in fallos[:20]:
            print("FAIL " + f, file=sys.stderr)
        return 1 if fallos else 0
    finally:
        servidor.terminate()
        try:
            servidor.wait(timeout=5)
        except subprocess.TimeoutExpired:
            servidor.kill()


if __name__ == "__main__":
    raise SystemExit(main())
