#!/usr/bin/env python3
"""Convierte matches/<carpeta>/posiciones.txt (formato compacto) en peticiones /move.

    python3 training-room/liga_posiciones.py matches/liga-0925/posiciones.txt tests/posiciones-liga

Formato de cada linea: partida|turno|comida|salud:cuerpo|...; '*' marca a la nuestra y el
cuerpo va cabeza primero, 'x,y' separados por espacios. Sirve cuando el motor no se puede
bajar entero y solo se traen las posiciones que interesan.
"""
import json
import os
import sys


def pts(s):
    return [{"x": int(a), "y": int(b)} for a, b in (p.split(",") for p in s.split())]


def main():
    origen, destino = sys.argv[1], sys.argv[2]
    os.makedirs(destino, exist_ok=True)
    for linea in open(origen, encoding="utf-8"):
        linea = linea.strip()
        if not linea or linea.startswith("#"):
            continue
        partes = linea.split("|")
        partida, turno, comida, serpientes = partes[0], int(partes[1]), partes[2], partes[3:]
        snakes, you = [], None
        for i, s in enumerate(serpientes):
            nuestra = s.startswith("*")
            salud, cuerpo = s.lstrip("*").split(":")
            body = pts(cuerpo)
            sn = {"id": f"s{i}", "name": "Makarov" if nuestra else f"rival{i}",
                  "health": int(salud), "body": body, "head": body[0],
                  "length": len(body), "latency": "0", "shout": ""}
            snakes.append(sn)
            if nuestra:
                you = sn
        req = {"game": {"id": partida, "ruleset": {"name": "standard", "version": "liga",
                        "settings": {"foodSpawnChance": 15, "minimumFood": 1,
                                     "hazardDamagePerTurn": 0}},
                        "map": "standard", "timeout": 500, "source": "ladder"},
               "turn": turno,
               "board": {"height": 11, "width": 11, "food": pts(comida), "hazards": [],
                         "snakes": snakes},
               "you": you}
        with open(os.path.join(destino, f"{partida}-t{turno}.json"), "w", encoding="utf-8") as f:
            json.dump(req, f, indent=1)
    print("ok")


if __name__ == "__main__":
    main()
