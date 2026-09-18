#!/usr/bin/env python3
"""Tests del orquestador. Se corren solos:  python3 training-room/test_tr.py

No necesitan docker ni arbitro: lo que se prueba es la derivacion, que es donde estan los
errores que mienten en silencio. Que un contenedor no arranque se ve; que los puestos
esten mal repartidos, no.
"""
import importlib.util
import json
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("tr", RAIZ / "training-room/tr.py")
tr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tr)

fallos = []


def comprueba(condicion, mensaje):
    if condicion:
        print(f"OK   {mensaje}")
    else:
        print(f"FAIL {mensaje}")
        fallos.append(mensaje)


def jsonl_falso(turnos_por_snake, ganador=None, empate=False):
    """Construye un JSONL con la forma que exporta el arbitro: cabecera, un estado por
    turno del que van desapareciendo las eliminadas, y una linea final con el ganador."""
    lineas = [json.dumps({"id": "x", "ruleset": {"name": "royale"}, "map": "royale"})]
    ultimo = max(turnos_por_snake.values())
    for turno in range(ultimo + 1):
        vivas = [{"id": sid, "name": sid, "health": 100, "length": 3, "latency": "1",
                  "body": [], "head": {"x": 0, "y": 0}}
                 for sid, t in turnos_por_snake.items() if t >= turno]
        lineas.append(json.dumps({"turn": turno, "board": {"snakes": vivas, "food": [],
                                                           "hazards": [], "width": 11, "height": 11},
                                  "you": {}, "game": {}}))
    lineas.append(json.dumps({"isDraw": empate, "winnerId": ganador, "winnerName": ganador or ""}))
    ruta = Path(tempfile.mkstemp(suffix=".jsonl")[1])
    ruta.write_text("\n".join(lineas) + "\n", encoding="utf-8")
    return ruta


# ---------------------------------------------------------------- puestos
# Un empate a cuatro tiene que repartir 2.5 a todos, no 1,2,3,4 por orden de aparicion:
# desempatar por indice es exactamente lo que el proyecto prohibe (ver docs/rules.md#r-12).
d = tr.lee_partida(jsonl_falso({"a": 9, "b": 9, "c": 9, "d": 9}), "/dev/null")
comprueba(set(d["puestos"].values()) == {2.5}, "cuatro eliminadas a la vez comparten puesto 2.5")
comprueba(abs(sum(d["puestos"].values()) - 10.0) < 1e-9, "los puestos de 4 suman 10")

d = tr.lee_partida(jsonl_falso({"a": 9, "b": 9, "c": 4, "d": 4}, ganador=None), "/dev/null")
comprueba(sorted(d["puestos"].values()) == [1.5, 1.5, 3.5, 3.5], "dos empates de dos dan 1.5 y 3.5")

d = tr.lee_partida(jsonl_falso({"a": 9, "b": 5, "c": 2}, ganador="a"), "/dev/null")
comprueba(d["puestos"]["a"] == 1.0 and d["puestos"]["b"] == 2.0 and d["puestos"]["c"] == 3.0,
          "sin empates, el orden es por turnos sobrevividos")

# ---------------------------------------------------------------- suma invariante
# La suma de puestos de n snakes es n(n+1)/2 pase lo que pase. Si un reparto la rompe, el
# error esta en el reparto y no en la partida.
for n in (2, 3, 4):
    for corte in range(1, n + 1):
        turnos = {chr(97 + i): (9 if i < corte else 4) for i in range(n)}
        d = tr.lee_partida(jsonl_falso(turnos), "/dev/null")
        if abs(sum(d["puestos"].values()) - n * (n + 1) / 2) > 1e-9:
            fallos.append(f"suma de puestos rota con n={n} corte={corte}")
comprueba(not [f for f in fallos if "suma de puestos rota" in f],
          "la suma de puestos es n(n+1)/2 en todas las combinaciones de 2, 3 y 4")

# ---------------------------------------------------------------- reparto de nucleos
cpusets, motivo = tr.reparte_nucleos(4, 1, fatal=False)
comprueba(len(set(cpusets)) == 4, "cuatro snakes reciben cuatro cpusets disjuntos")
_, motivo = tr.reparte_nucleos(1000, 1, fatal=False)
comprueba(motivo is not None, "un torneo que no cabe en la maquina se rechaza")
_, motivo = tr.reparte_nucleos(2, 0.5, fatal=False)
comprueba(motivo is not None, "una cuota fraccionaria de CPU se rechaza: compartiria nucleo")

# ---------------------------------------------------------------- partida real
real = RAIZ / "docs/results/2026-09-15-v0-vs-eremetic-eric.jsonl"
if real.exists():
    d = tr.lee_partida(real, "/dev/null")
    comprueba(d["puestos"][d["ganador"]] == 1.0, "en una partida real, el ganador queda primero")
    comprueba(len(d["latencias"][d["ganador"]]) == d["turnos"],
              "hay una latencia por turno jugado del ganador")

print()
if fallos:
    print(f"{len(fallos)} fallos")
    sys.exit(1)
print("todos los tests del orquestador pasan")
