#!/usr/bin/env python3
"""Fuzz de los parsers del orquestador: la metrica `fuzz_states` de su clase robustness.

    python3 scripts/fuzz_tr.py --estados 10000 [--semilla 1]

Que ataca y por que:

El Training Room lee dos cosas que NO controla: el JSONL que escribe el arbitro oficial y
su stderr. Ambos vienen de un binario de Go que puede cambiar de version, cortarse a media
partida si se mata el proceso, o quedarse a cero si dos corridas se pisan -que ya paso-.

Un parser que revienta con un fichero raro no es lo peor. Lo peor es el que devuelve algo
plausible: un torneo que resume 200 partidas y en 12 leyo basura sin decirlo produce una
tabla con la que se toman decisiones. Asi que aqui no se exige que no lance, se exige lo
concreto:

  - `lee_partida` devuelve None o un resultado COHERENTE: los puestos suman n(n+1)/2,
    ningun puesto se sale del rango, y las latencias son numeros;
  - `incidencias_de` nunca cuenta mas incidencias que lineas tiene el fichero;
  - ninguna de las dos lanza una excepcion no controlada.

Determinista: misma semilla, mismos estados.
"""
import argparse
import importlib.util
import json
import pathlib
import random
import sys
import tempfile

RAIZ = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("tr", RAIZ / "training-room/tr.py")
tr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tr)

DIRECTOR = ("INFO 03:02:08.578366 Snake ID: {sid} URL: http://127.0.0.1:{p}, Name: \"{n}\"")


def jsonl_valido(rng, n_snakes=4, turnos=None):
    turnos = turnos if turnos is not None else rng.randint(2, 30)
    ids = [f"id{i}" for i in range(n_snakes)]
    muere_en = {i: rng.randint(1, turnos) for i in ids}
    lineas = [json.dumps({"id": "x", "ruleset": {"name": "royale"}, "map": "royale"})]
    for t in range(turnos):
        vivas = [{"id": i, "name": i, "health": rng.randint(0, 100), "length": 3,
                  "latency": str(rng.randint(0, 500)),
                  "body": [{"x": 0, "y": 0}], "head": {"x": 0, "y": 0}}
                 for i in ids if muere_en[i] >= t]
        lineas.append(json.dumps({"turn": t, "you": {}, "game": {},
                                  "board": {"snakes": vivas, "food": [], "hazards": [],
                                            "width": 11, "height": 11}}))
    lineas.append(json.dumps({"isDraw": False, "winnerId": ids[0], "winnerName": ids[0]}))
    return lineas


def rompe(rng, lineas):
    """Le hace a un JSONL valido lo que le pasaria de verdad: cortarse, truncarse,
    llenarse de ceros, perder un campo, o traer un tipo que no toca."""
    lineas = list(lineas)
    for _ in range(rng.randint(1, 3)):
        if not lineas:
            break
        cual = rng.random()
        i = rng.randrange(len(lineas))
        if cual < 0.15:
            lineas = lineas[:i]                                  # se corto a media partida
        elif cual < 0.30:
            lineas[i] = lineas[i][:rng.randint(0, len(lineas[i]))]   # linea truncada
        elif cual < 0.40:
            lineas[i] = ""                                       # linea vacia
        elif cual < 0.50:
            lineas[i] = "\x00" * rng.randint(1, 20)              # basura binaria
        elif cual < 0.60:
            lineas[i] = "{}"                                     # objeto sin nada
        elif cual < 0.75:
            try:
                doc = json.loads(lineas[i])
                claves = list(doc.keys()) if isinstance(doc, dict) else []
                if claves:
                    doc.pop(rng.choice(claves))
                lineas[i] = json.dumps(doc)
            except ValueError:
                pass
        elif cual < 0.90:
            try:
                doc = json.loads(lineas[i])
                if not isinstance(doc, dict):
                    continue
                if isinstance(doc.get("board"), dict):
                    doc["board"]["snakes"] = rng.choice(
                        [None, {}, "snakes", [1, 2], [{"id": None}], []])
                else:
                    doc[rng.choice(list(doc.keys()) or ["x"])] = None
                lineas[i] = json.dumps(doc)
            except (ValueError, AttributeError):
                pass
        else:
            lineas.insert(i, rng.choice(["null", "[]", "3", '"turno"', "{"]))
    return lineas


def coherente(d, esperadas):
    """Un resultado que no lanzo tiene que ser ademas utilizable."""
    if d is None:
        return True, ""
    puestos = d.get("puestos", {})
    if puestos:
        n = len(puestos)
        suma = sum(puestos.values())
        if abs(suma - n * (n + 1) / 2) > 1e-6:
            return False, f"los puestos de {n} suman {suma}"
        for p in puestos.values():
            if not (1.0 <= p <= n):
                return False, f"puesto {p} fuera de rango con {n} snakes"
    for lista in d.get("latencias", {}).values():
        for v in lista:
            if not isinstance(v, float):
                return False, f"latencia no numerica: {v!r}"
    if not isinstance(d.get("turnos", 0), int):
        return False, "turnos no es entero"
    return True, ""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--estados", type=int, default=10000)
    ap.add_argument("--semilla", type=int, default=1)
    args = ap.parse_args()

    rng = random.Random(args.semilla)
    tmp = pathlib.Path(tempfile.mkdtemp())
    jsonl = tmp / "p.jsonl"
    reflog = tmp / "p.ref.log"
    fallos = []
    incoherentes = 0
    devolvieron_none = 0

    for i in range(args.estados):
        lineas = rompe(rng, jsonl_valido(rng))
        jsonl.write_text("\n".join(lineas) + "\n", encoding="utf-8", errors="replace")

        n_log = rng.randint(0, 6)
        log = [DIRECTOR.format(sid=f"id{j}", p=9700 + j, n=f"id{j}") for j in range(n_log)]
        for _ in range(rng.randint(0, 5)):
            log.insert(rng.randrange(len(log) + 1),
                       rng.choice(["WARN Request to http://127.0.0.1:9700/move failed",
                                   "ERROR context deadline exceeded",
                                   "\x00\x01", "", "INFO Turn: 3"]))
        reflog.write_text("\n".join(log) + "\n", encoding="utf-8", errors="replace")

        try:
            d = tr.lee_partida(jsonl, reflog)
        except Exception as exc:
            fallos.append(f"estado {i}: lee_partida lanzo {type(exc).__name__}: {exc}")
            continue
        if d is None:
            devolvieron_none += 1
        ok, motivo = coherente(d, 4)
        if not ok:
            incoherentes += 1
            fallos.append(f"estado {i}: resultado incoherente - {motivo}")

        try:
            inc = tr.incidencias_de(reflog, "http://127.0.0.1:9700")
        except Exception as exc:
            fallos.append(f"estado {i}: incidencias_de lanzo {type(exc).__name__}: {exc}")
            continue
        total_lineas = len(log)
        if sum(inc.values()) > total_lineas:
            fallos.append(f"estado {i}: {sum(inc.values())} incidencias en {total_lineas} lineas")

        if len(fallos) > 20:
            break

    resultado = {
        "fuzz_states": args.estados if not fallos else i + 1,
        "semilla": args.semilla,
        "excepciones": sum(1 for f in fallos if "lanzo" in f),
        "resultados_incoherentes": incoherentes,
        "devolvieron_none": devolvieron_none,
    }
    print(json.dumps(resultado, indent=2, ensure_ascii=False))
    for f in fallos[:20]:
        print("FAIL " + f, file=sys.stderr)
    return 1 if fallos else 0


if __name__ == "__main__":
    raise SystemExit(main())
