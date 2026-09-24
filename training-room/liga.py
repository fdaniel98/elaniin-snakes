#!/usr/bin/env python3
"""Analiza EN BLOQUE partidas reales (leaderboard o torneo) ya descargadas.

    ./scripts/bajar-partidas.sh partidas.txt
    python3 training-room/liga.py matches/liga --nuestra Makarov

`partida.py` mira UNA partida turno a turno; esto mira muchas y busca el patron: contra
quien perdemos, en que fase, por que causa, y con que numeros llegabamos a ese punto.
Es la version para datos reales de lo que `training-room/compara.py` hace con corridas
controladas -y a diferencia de aquella no da veredictos: en el leaderboard ni el campo ni
las semillas son nuestros, asi que esto DESCRIBE, no decide. ver docs/experimentos-duelo.md

Que saca:

- resultado por rival: partidas, puesto medio, y cuantas veces ese rival nos sobrevivio;
- fase en que salimos: con cuatro vivas, con tres, o en el 1v1 final;
- causa de muerte deducida del ultimo frame: encerrados, cabezazo, hambre o "habia salida";
- y para los duelos, los mismos numeros con los que comparamos a snork: distancia a la
  cola, distancia al centro y espacio alcanzable en los turnos tardios.
"""

import argparse
import collections
import glob
import json
import os
import statistics as st
from collections import deque

D = [(0, 1), (0, -1), (1, 0), (-1, 0)]


def carga(carpeta):
    """Une las paginas de frames de cada partida. Devuelve [(id, [frames ordenados])]."""
    juegos = []
    for dir_partida in sorted(glob.glob(os.path.join(carpeta, "*"))):
        if not os.path.isdir(dir_partida):
            continue
        frames = {}
        for pagina in glob.glob(os.path.join(dir_partida, "*.json")):
            with open(pagina, encoding="utf-8") as f:
                doc = json.load(f)
            for fr in doc.get("Frames", doc if isinstance(doc, list) else []):
                frames[fr["Turn"]] = fr
        if frames:
            juegos.append((os.path.basename(dir_partida), [frames[t] for t in sorted(frames)]))
    return juegos


def pos(p):
    return (p["X"], p["Y"])


def vivas(fr):
    return [s for s in fr["Snakes"] if not s.get("Death")]


def ocupadas(snakes):
    occ = set()
    for s in snakes:
        b = [pos(p) for p in s["Body"]]
        occ |= set(b if len(b) >= 2 and b[-1] == b[-2] else b[:-1])
    return occ


def region(occ, inicio, w, h):
    visto = {inicio}
    q = deque([inicio])
    while q:
        x, y = q.popleft()
        for dx, dy in D:
            n = (x + dx, y + dy)
            if 0 <= n[0] < w and 0 <= n[1] < h and n not in occ and n not in visto:
                visto.add(n)
                q.append(n)
    return visto


def distancia_cola(s, occ, w, h):
    b = [pos(p) for p in s["Body"]]
    cabeza, cola = b[0], b[-1]
    visto = {cabeza}
    q = deque([(cabeza, 0)])
    while q:
        (x, y), d = q.popleft()
        for dx, dy in D:
            n = (x + dx, y + dy)
            if not (0 <= n[0] < w and 0 <= n[1] < h) or n in visto:
                continue
            if n == cola:
                return d + 1
            if n in occ:
                continue
            visto.add(n)
            q.append((n, d + 1))
    return -1


def causa(fr, nos, w, h):
    """Por que morimos, deducido del ultimo frame en que seguiamos vivos."""
    if nos["Health"] <= 1:
        return "hambre"
    vs = vivas(fr)
    occ = ocupadas(vs)
    cabeza = pos(nos["Body"][0])
    rivales = [s for s in vs if s is not nos]
    zonas = set()
    for r in rivales:
        rh = pos(r["Body"][0])
        if len(r["Body"]) >= len(nos["Body"]):
            zonas |= {(rh[0] + dx, rh[1] + dy) for dx, dy in D}
    salidas = []
    for dx, dy in D:
        n = (cabeza[0] + dx, cabeza[1] + dy)
        if 0 <= n[0] < w and 0 <= n[1] < h and n not in occ:
            salidas.append(n)
    if not salidas:
        return "encerrada"
    if all(s in zonas for s in salidas):
        return "solo_cabezazo"
    return "habia_salida"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("carpeta", help="carpeta con una subcarpeta por partida")
    ap.add_argument("--nuestra", required=True, help="nombre de nuestra snake en las partidas")
    ap.add_argument("--desde-turno", type=int, default=250,
                    help="turno a partir del cual se miden los numeros del final")
    args = ap.parse_args()

    juegos = carga(args.carpeta)
    if not juegos:
        raise SystemExit(f"sin partidas en {args.carpeta}")

    por_rival = collections.defaultdict(lambda: {"n": 0, "puestos": [], "nos_sobrevive": 0})
    causas = collections.Counter()
    fases = collections.Counter()
    puestos = []
    tardio = collections.defaultdict(list)
    sin_nosotros = 0

    for _, frames in juegos:
        ultimo = frames[-1]
        nombres = [s["Name"] for s in ultimo["Snakes"]]
        if args.nuestra not in nombres:
            sin_nosotros += 1
            continue
        w = 11
        h = 11
        yo_final = next(s for s in ultimo["Snakes"] if s["Name"] == args.nuestra)
        muerte = yo_final.get("Death")
        total = len(ultimo["Snakes"])
        # Puesto: 1 si sobrevivimos; si no, 1 + cuantos murieron despues que nosotros.
        if muerte is None:
            puesto = 1
        else:
            despues = sum(1 for s in ultimo["Snakes"]
                          if s is not yo_final and (s.get("Death") is None
                                                    or s["Death"]["Turn"] > muerte["Turn"]))
            puesto = 1 + despues
        puestos.append(puesto)

        for s in ultimo["Snakes"]:
            if s["Name"] == args.nuestra:
                continue
            d = por_rival[s["Name"]]
            d["n"] += 1
            d["puestos"].append(puesto)
            suyo = s.get("Death")
            if muerte is not None and (suyo is None or suyo["Turn"] > muerte["Turn"]):
                d["nos_sobrevive"] += 1

        if muerte is not None:
            ult_vivo = None
            for fr in frames:
                if any(s["Name"] == args.nuestra for s in vivas(fr)):
                    ult_vivo = fr
            if ult_vivo is not None:
                nos = next(s for s in vivas(ult_vivo) if s["Name"] == args.nuestra)
                causas[causa(ult_vivo, nos, w, h)] += 1
                fases[f"{len(vivas(ult_vivo))} vivas"] += 1

        for fr in frames:
            if fr["Turn"] < args.desde_turno:
                continue
            vs = vivas(fr)
            nos = next((s for s in vs if s["Name"] == args.nuestra), None)
            if nos is None or len(vs) != 2:
                continue
            occ = ocupadas(vs)
            cab = pos(nos["Body"][0])
            rival = next(s for s in vs if s is not nos)
            for etiqueta, snake in ((args.nuestra, nos), ("rival", rival)):
                c = pos(snake["Body"][0])
                dc = distancia_cola(snake, occ, w, h)
                clave = "nuestra" if etiqueta == args.nuestra else "rival"
                tardio[clave + ":cola"].append(dc)
                tardio[clave + ":centro"].append(abs(c[0] - 5) + abs(c[1] - 5))
                tardio[clave + ":espacio"].append(len(region(occ, c, w, h)) - 1)
            del cab

    print(f"partidas: {len(puestos)}  (descartadas por no encontrarnos: {sin_nosotros})")
    if puestos:
        print(f"puesto medio: {st.mean(puestos):.3f}   primeros: "
              f"{sum(1 for p in puestos if p == 1)}/{len(puestos)}")
    print("\n-- por rival (los que mas veces coincidimos) --")
    print(f"{'rival':<26}{'partidas':>9}{'puesto medio':>14}{'nos sobrevive':>15}")
    for nombre, d in sorted(por_rival.items(), key=lambda kv: -kv[1]["n"])[:15]:
        print(f"{nombre:<26}{d['n']:>9}{st.mean(d['puestos']):>14.3f}"
              f"{d['nos_sobrevive'] / d['n']:>14.0%}")
    print("\n-- como morimos --")
    for k, v in causas.most_common():
        print(f"  {k:<16}{v:>4}")
    print("\n-- en que fase salimos --")
    for k, v in fases.most_common():
        print(f"  {k:<16}{v:>4}")
    if tardio:
        print(f"\n-- duelos, turno >= {args.desde_turno} --")
        print(f"{'':<10}{'cola<=2':>9}{'cola alcanzable':>17}{'d.centro':>10}{'espacio':>9}")
        for quien in ("nuestra", "rival"):
            cola = tardio[quien + ":cola"]
            if not cola:
                continue
            print(f"{quien:<10}{sum(1 for c in cola if 0 <= c <= 2) / len(cola):>9.0%}"
                  f"{sum(1 for c in cola if c >= 0) / len(cola):>17.0%}"
                  f"{st.mean(tardio[quien + ':centro']):>10.2f}"
                  f"{st.mean(tardio[quien + ':espacio']):>9.1f}")


if __name__ == "__main__":
    main()
