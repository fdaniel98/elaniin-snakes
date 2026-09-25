#!/usr/bin/env python3
"""Pregunta a VARIAS snakes que harian en las mismas posiciones, y compara.

    ./scripts/zoo.sh up snork-tree --port 8121
    PORT=8120 ./build/release/bin/battlesnake-server &
    python3 training-room/consulta.py tests/posiciones-criticas \\
        --snake nuestra=http://127.0.0.1:8120 --snake tree=http://127.0.0.1:8121

Por que existe: snork Tree nos gana el torneo (ver docs/experimentos.md#s-standard-r) y lo
unico que se puede aprender de una snake ajena sin tocar su codigo es lo que HACE. Esto le
da las posiciones donde nosotros morimos y anota su movimiento junto al nuestro.

Cada posicion se le manda TAL CUAL a las dos, cambiando solo `you`, que es lo que el motor
haria con cada una. Donde coinciden no hay nada que aprender; donde difieren, el fichero de
salida da la lista de posiciones a mirar, y las columnas describen que tiene la casilla que
eligio cada una: espacio alcanzable despues del movimiento, si su propia cola sigue siendo
alcanzable, cuantas salidas tiene el destino y si se acerca o se aleja del rival.

Es observacion de comportamiento, no ingenieria inversa: no se lee ni se copia su codigo.
"""

import argparse
import collections
import json
import urllib.error
import urllib.request
from collections import deque
from pathlib import Path

D = {"up": (0, 1), "down": (0, -1), "left": (-1, 0), "right": (1, 0)}


def pide(url, payload, timeout=5.0):
    datos = json.dumps(payload).encode()
    req = urllib.request.Request(url.rstrip("/") + "/move", data=datos,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.load(r).get("move")
    except (urllib.error.URLError, OSError, ValueError) as e:
        return f"ERROR: {e}"


def pos(p):
    return (p["x"], p["y"])


def ocupadas(snakes):
    o = set()
    for s in snakes:
        b = [pos(q) for q in s["body"]]
        o |= set(b if len(b) >= 2 and b[-1] == b[-2] else b[:-1])
    return o


def region(occ, inicio, w, h):
    vista = {inicio}
    q = deque([inicio])
    while q:
        x, y = q.popleft()
        for dx, dy in D.values():
            n = (x + dx, y + dy)
            if 0 <= n[0] < w and 0 <= n[1] < h and n not in occ and n not in vista:
                vista.add(n)
                q.append(n)
    return vista


def describe(doc, quien, movimiento):
    """Que tiene la casilla elegida: espacio, salidas, cola alcanzable y acercamiento."""
    if movimiento not in D:
        return {}
    b = doc["board"]
    w, h = b["width"], b["height"]
    yo = next(s for s in b["snakes"] if s["name"] == quien)
    otros = [s for s in b["snakes"] if s is not yo]
    occ = ocupadas(b["snakes"])
    cab = pos(yo["head"])
    dx, dy = D[movimiento]
    destino = (cab[0] + dx, cab[1] + dy)
    if not (0 <= destino[0] < w and 0 <= destino[1] < h):
        return {"legal": False}
    libre = set(occ)
    libre.discard(pos(yo["body"][-1]))  # la cola avanza
    alcanzable = region(libre - {destino}, destino, w, h) if destino not in libre else set()
    salidas = sum(1 for ddx, ddy in D.values()
                  if 0 <= destino[0] + ddx < w and 0 <= destino[1] + ddy < h
                  and (destino[0] + ddx, destino[1] + ddy) not in occ)
    cola = pos(yo["body"][-1])
    d_rival = min((abs(destino[0] - pos(s["head"])[0]) + abs(destino[1] - pos(s["head"])[1])
                   for s in otros), default=99)
    d_rival_antes = min((abs(cab[0] - pos(s["head"])[0]) + abs(cab[1] - pos(s["head"])[1])
                         for s in otros), default=99)
    return {"legal": destino not in occ,
            "espacio": len(alcanzable),
            "salidas": salidas,
            "cola_alcanzable": cola in alcanzable,
            "d_rival": d_rival,
            "se_acerca": d_rival < d_rival_antes}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("posiciones")
    ap.add_argument("--snake", action="append", required=True,
                    metavar="nombre=url", help="repetir por cada snake a consultar")
    ap.add_argument("--como", default=None,
                    help="nombre de la snake del tablero por la que preguntan TODAS "
                         "(por defecto, el `you` que traiga el fichero)")
    ap.add_argument("--out", help="JSON con el detalle por posicion")
    args = ap.parse_args()

    snakes = {}
    for par in args.snake:
        if "=" not in par:
            raise SystemExit(f"--snake espera nombre=url, no {par}")
        nombre, url = par.split("=", 1)
        snakes[nombre] = url

    ficheros = sorted(Path(args.posiciones).glob("*.json"))
    if not ficheros:
        raise SystemExit(f"sin posiciones en {args.posiciones}")

    detalle, acuerdos = [], collections.Counter()
    rasgos = {n: collections.Counter() for n in snakes}
    espacios = {n: [] for n in snakes}
    for f in ficheros:
        doc = json.loads(f.read_text(encoding="utf-8"))
        quien = args.como or doc["you"]["name"]
        doc = dict(doc)
        doc["you"] = next(s for s in doc["board"]["snakes"] if s["name"] == quien)
        fila = {"posicion": f.name, "turno": doc.get("turn")}
        for nombre, url in snakes.items():
            mov = pide(url, doc)
            fila[nombre] = mov
            d = describe(doc, quien, mov)
            fila[f"{nombre}_rasgos"] = d
            if d.get("legal"):
                espacios[nombre].append(d["espacio"])
                rasgos[nombre]["cola_alcanzable"] += 1 if d["cola_alcanzable"] else 0
                rasgos[nombre]["se_acerca"] += 1 if d["se_acerca"] else 0
                rasgos[nombre]["salidas>=3"] += 1 if d["salidas"] >= 3 else 0
                rasgos[nombre]["legales"] += 1
        movs = {fila[n] for n in snakes}
        acuerdos["coinciden" if len(movs) == 1 else "difieren"] += 1
        detalle.append(fila)

    print(f"posiciones: {len(detalle)}   {dict(acuerdos)}")
    print(f"\n{'snake':<12}{'legales':>9}{'espacio medio':>15}{'cola alcanzable':>17}"
          f"{'se acerca':>11}{'salidas>=3':>12}")
    for n in snakes:
        r = rasgos[n]
        leg = max(r["legales"], 1)
        media = sum(espacios[n]) / len(espacios[n]) if espacios[n] else 0
        print(f"{n:<12}{r['legales']:>9}{media:>15.1f}{r['cola_alcanzable'] / leg:>16.0%}"
              f"{r['se_acerca'] / leg:>11.0%}{r['salidas>=3'] / leg:>12.0%}")

    if args.out:
        Path(args.out).write_text(json.dumps(detalle, indent=1), encoding="utf-8")
        print(f"\ndetalle en {args.out}")


if __name__ == "__main__":
    main()
