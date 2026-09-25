#!/usr/bin/env python3
"""Saca de un torneo las posiciones donde una derrota se vuelve irreversible.

    python3 training-room/posiciones.py docs/results/torneo-standard-v5 \\
        --nuestra v5-longitud --rival snork-tree --out tests/posiciones-criticas

Por que existe: en los duelos perdidos contra Tree el espacio libre cae de 2.5 veces
nuestro cuerpo a CERO en los ultimos 20 turnos, con la salud casi intacta
(ver docs/experimentos-duelo.md#s-derrumbe). O sea que hay un turno concreto donde se
elige mal y el resto es inercia. Esto lo localiza y lo guarda como payload de `/move`,
igual formato que `tests/fixtures/`, para que cualquier candidato se pueda puntuar sobre
ellas con `bin/sonda_posiciones` en segundos en vez de un torneo de horas.

El criterio: el primer turno del duelo en que `espacio_alcanzable / longitud` baja del
umbral y ya no vuelve a subir por encima de el. Es una definicion operativa, no una
verdad: el umbral se declara en la linea de comandos y va en el nombre del fichero.
"""

import argparse
import json
import statistics as st
from collections import deque
from pathlib import Path

D = [(0, 1), (0, -1), (1, 0), (-1, 0)]
W = H = 11


def pos(p):
    return (p["x"], p["y"])


def ocupadas(snakes):
    o = set()
    for s in snakes:
        b = [pos(q) for q in s["body"]]
        o |= set(b if len(b) >= 2 and b[-1] == b[-2] else b[:-1])
    return o


def region(occ, inicio):
    vista = {inicio}
    q = deque([inicio])
    while q:
        x, y = q.popleft()
        for dx, dy in D:
            n = (x + dx, y + dy)
            if 0 <= n[0] < W and 0 <= n[1] < H and n not in occ and n not in vista:
                vista.add(n)
                q.append(n)
    return vista


def a_request(estado, nombre):
    """El payload de /move tal y como lo recibiria el servidor, visto por `nombre`."""
    nos = next(s for s in estado["board"]["snakes"] if s["name"] == nombre)
    doc = json.loads(json.dumps(estado))  # copia
    doc["you"] = nos
    return doc


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("torneo")
    ap.add_argument("--nuestra", required=True)
    ap.add_argument("--rival", help="si se da, solo los duelos contra este rival")
    ap.add_argument("--umbral", type=float, default=1.2,
                    help="espacio/longitud por debajo del cual la posicion se considera perdida")
    ap.add_argument("--antes-de-morir", type=int, metavar="N",
                    help="en vez del umbral, saca la posicion N turnos ANTES de morir. Sirve "
                         "para mirar donde la partida todavia estaba abierta: en el punto "
                         "del derrumbe, snork Tree elige lo mismo que nosotros en 39 de 48 "
                         "(ver docs/experimentos-duelo.md#s-consulta-r)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    salida = Path(args.out)
    salida.mkdir(parents=True, exist_ok=True)
    sacadas, margenes = 0, []
    for ruta in sorted(Path(args.torneo).glob("*.jsonl")):
        lineas = [json.loads(l) for l in ruta.read_text(encoding="utf-8").splitlines() if l.strip()]
        estados = [x for x in lineas if "board" in x]
        if not estados:
            continue
        ganador = lineas[-1].get("winnerName")
        if ganador == args.nuestra:
            continue
        duelo = [x for x in estados
                 if len(x["board"]["snakes"]) == 2
                 and any(s["name"] == args.nuestra for s in x["board"]["snakes"])
                 and (args.rival is None
                      or any(s["name"] == args.rival for s in x["board"]["snakes"]))]
        if len(duelo) < 10:
            continue
        if args.antes_de_morir is not None:
            objetivo = duelo[-1]["turn"] - args.antes_de_morir
            candidato = min(duelo, key=lambda x: abs(x["turn"] - objetivo))
            if abs(candidato["turn"] - objetivo) > 5:
                continue  # ese duelo no llego a durar tanto; no se estira
            destino = salida / f"{ruta.stem}-t{candidato['turn']}.json"
            doc = a_request(candidato, args.nuestra)
            doc["_comment"] = (f"Turno {candidato['turn']} de {ruta.stem}: "
                               f"{args.antes_de_morir} turnos antes de nuestra muerte, con la "
                               f"partida todavia abierta. Extraida por posiciones.py.")
            destino.write_text(json.dumps(doc, indent=1), encoding="utf-8")
            sacadas += 1
            margenes.append(duelo[-1]["turn"] - candidato["turn"])
            continue

        # Primer turno por debajo del umbral que ya no se recupera.
        candidato = None
        for x in duelo:
            nos = next(s for s in x["board"]["snakes"] if s["name"] == args.nuestra)
            occ = ocupadas(x["board"]["snakes"])
            ratio = (len(region(occ, pos(nos["head"]))) - 1) / max(len(nos["body"]), 1)
            if ratio < args.umbral:
                candidato = candidato or x
            else:
                candidato = None
        if candidato is None:
            continue
        destino = salida / f"{ruta.stem}-t{candidato['turn']}.json"
        doc = a_request(candidato, args.nuestra)
        doc["_comment"] = (f"Turno {candidato['turn']} de {ruta.stem}: aqui el espacio "
                           f"alcanzable cae por debajo de {args.umbral} veces nuestro cuerpo "
                           f"y la derrota ya no se revierte. Extraida por posiciones.py.")
        destino.write_text(json.dumps(doc, indent=1), encoding="utf-8")
        sacadas += 1
        margenes.append(duelo[-1]["turn"] - candidato["turn"])

    print(f"posiciones guardadas: {sacadas} en {salida}")
    if margenes:
        print(f"turnos entre la posicion y la muerte: mediana {st.median(margenes):.0f}  "
              f"minimo {min(margenes)}  maximo {max(margenes)}")


if __name__ == "__main__":
    main()
