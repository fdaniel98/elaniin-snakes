#!/usr/bin/env python3
"""Analiza una partida REAL a partir de los frames del motor de Battlesnake.

    python3 training-room/partida.py matches/match-01*.json --nuestra Makarov
    python3 training-room/partida.py matches/*.json --nuestra Makarov \\
        --servidor http://127.0.0.1:8080 --desde 200

Los frames son los de `https://engine.battlesnake.com/games/<id>/frames?offset=N&limit=100`:
el motor devuelve como mucho 100 por pagina, asi que una partida larga son varias paginas.
Se pasan todas y aqui se juntan por `Turn`, sin importar el orden ni los solapes.

Que saca, por turno:

- **territorio** de cada snake: casillas a las que llega antes que el rival, por BFS
  simultaneo desde las cabezas (los empates no son de nadie). Es la misma idea que el
  Voronoi de `evaluate`, recalculada aqui en Python para no depender del binario;
- **espacio**: casillas alcanzables desde nuestra cabeza, sin contar rivales;
- el movimiento que hizo nuestra snake, y con `--servidor` el que elegiria el cerebro de
  ESTE arbol si se le enseña la misma posicion. Si no coinciden, el comportamiento ya no es
  el de la partida (otro commit, otro reloj) y la comparacion hay que leerla con eso.

No arbitra nada ni sustituye al arbitro oficial: es un visor con numeros, para encontrar
el turno en que la partida se decidio. ver docs/context-packs/debug-partida.md
"""

import argparse
import json
import sys
import urllib.request
from collections import deque

MOV = {(0, 1): "up", (0, -1): "down", (1, 0): "right", (-1, 0): "left"}


def carga(rutas):
    frames = {}
    for r in rutas:
        with open(r, encoding="utf-8") as f:
            d = json.load(f)
        for fr in d.get("Frames", d if isinstance(d, list) else []):
            frames[fr["Turn"]] = fr
    return [frames[t] for t in sorted(frames)]


def vivas(fr):
    return [s for s in fr["Snakes"] if not s.get("Death")]


def cuerpo(s):
    return [(p["X"], p["Y"]) for p in s["Body"]]


def ocupadas(snakes):
    """Casillas que siguen ocupadas el turno siguiente: la cola avanza salvo si esta apilada."""
    occ = set()
    for s in snakes:
        b = cuerpo(s)
        apilada = len(b) >= 2 and b[-1] == b[-2]
        occ.update(b if apilada else b[:-1])
    return occ


def territorio(snakes, w, h):
    occ = ocupadas(snakes)
    dueno = {}
    q = deque()
    for s in snakes:
        cabeza = cuerpo(s)[0]
        dueno[cabeza] = (s["Name"], 0)
        q.append((cabeza, s["Name"], 0))
    while q:
        (x, y), quien, d = q.popleft()
        if dueno.get((x, y), (None,))[0] is None:
            continue  # la casilla quedo empatada despues de encolarla
        for dx, dy in MOV:
            n = (x + dx, y + dy)
            if not (0 <= n[0] < w and 0 <= n[1] < h) or n in occ:
                continue
            if n in dueno:
                otro, dist = dueno[n]
                if dist == d + 1 and otro not in (quien, None):
                    dueno[n] = (None, d + 1)
                continue
            dueno[n] = (quien, d + 1)
            q.append((n, quien, d + 1))
    cuenta = {}
    for quien, _ in dueno.values():
        if quien is not None:
            cuenta[quien] = cuenta.get(quien, 0) + 1
    return cuenta


def espacio(snakes, nuestra, w, h):
    occ = ocupadas(snakes)
    cabeza = cuerpo(nuestra)[0]
    visto = {cabeza}
    q = deque([cabeza])
    while q:
        x, y = q.popleft()
        for dx, dy in MOV:
            n = (x + dx, y + dy)
            if 0 <= n[0] < w and 0 <= n[1] < h and n not in occ and n not in visto:
                visto.add(n)
                q.append(n)
    return len(visto) - 1


def a_request(fr, nuestra, args):
    def snake_api(s):
        b = [{"x": x, "y": y} for x, y in cuerpo(s)]
        return {"id": s["ID"], "name": s["Name"], "health": s["Health"], "body": b,
                "head": b[0], "length": len(b), "latency": s.get("Latency") or "0",
                "shout": "", "squad": ""}
    return {
        "game": {"id": "replay", "timeout": args.timeout, "source": "replay", "map": args.map,
                 "ruleset": {"name": args.ruleset, "version": "replay",
                             "settings": {"foodSpawnChance": 15, "minimumFood": 1,
                                          "hazardDamagePerTurn": 14,
                                          "royale": {"shrinkEveryNTurns": 25}}}},
        "turn": fr["Turn"],
        "board": {"width": args.ancho, "height": args.alto,
                  "food": [{"x": p["X"], "y": p["Y"]} for p in fr.get("Food") or []],
                  "hazards": [{"x": p["X"], "y": p["Y"]} for p in fr.get("Hazards") or []],
                  "snakes": [snake_api(s) for s in vivas(fr)]},
        "you": snake_api(nuestra),
    }


def pregunta(servidor, req):
    datos = json.dumps(req).encode()
    r = urllib.request.Request(servidor.rstrip("/") + "/move", data=datos,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=5) as resp:
        return json.load(resp).get("move", "?")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("frames", nargs="+")
    ap.add_argument("--nuestra", required=True, help="nombre de nuestra snake en la partida")
    ap.add_argument("--servidor", help="URL de un battlesnake-server local para comparar")
    ap.add_argument("--desde", type=int, default=0)
    ap.add_argument("--hasta", type=int, default=10**9)
    ap.add_argument("--ruleset", default="standard")
    ap.add_argument("--map", default="standard")
    ap.add_argument("--timeout", type=int, default=500)
    ap.add_argument("--ancho", type=int, default=11)
    ap.add_argument("--alto", type=int, default=11)
    args = ap.parse_args()

    frames = carga(args.frames)
    if not frames:
        sys.exit("sin frames")
    huecos = [t for t in range(frames[0]["Turn"], frames[-1]["Turn"] + 1)
              if t not in {f["Turn"] for f in frames}]
    print(f"turnos {frames[0]['Turn']}..{frames[-1]['Turn']}"
          + (f"  FALTAN {len(huecos)} (primer hueco {huecos[0]})" if huecos else ""))
    ultimo = frames[-1]
    for s in ultimo["Snakes"]:
        d = s.get("Death")
        print(f"  {s['Name']:<14} " + (f"murio turno {d['Turn']}: {d['Cause']}" if d else "viva al final"))

    cab = "turno  largo  rival  salud  territ  rival_t  espacio  hizo   cerebro"
    print("\n" + cab)
    siguiente = {f["Turn"]: f for f in frames}
    for fr in frames:
        t = fr["Turn"]
        if t < args.desde or t > args.hasta:
            continue
        vs = vivas(fr)
        nos = next((s for s in vs if s["Name"] == args.nuestra), None)
        if nos is None:
            continue
        rivales = [s for s in vs if s is not nos]
        terr = territorio(vs, args.ancho, args.alto)
        largo_r = max((len(s["Body"]) for s in rivales), default=0)
        terr_r = max((terr.get(s["Name"], 0) for s in rivales), default=0)
        hizo = "-"
        nx = siguiente.get(t + 1)
        if nx:
            yo2 = next((s for s in nx["Snakes"] if s["Name"] == args.nuestra), None)
            if yo2:
                a, b = cuerpo(nos)[0], cuerpo(yo2)[0]
                hizo = MOV.get((b[0] - a[0], b[1] - a[1]), "?")
        cerebro = ""
        if args.servidor:
            try:
                cerebro = pregunta(args.servidor, a_request(fr, nos, args))
                if cerebro != hizo and hizo != "-":
                    cerebro += "  <-- distinto"
            except Exception as e:  # noqa: BLE001 - un visor no debe caerse por un turno
                cerebro = f"error: {e}"
        print(f"{t:5d}  {len(nos['Body']):5d}  {largo_r:5d}  {nos['Health']:5d}  "
              f"{terr.get(nos['Name'], 0):6d}  {terr_r:7d}  {espacio(vs, nos, args.ancho, args.alto):7d}"
              f"  {hizo:<5}  {cerebro}")


if __name__ == "__main__":
    main()
