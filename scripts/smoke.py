#!/usr/bin/env python3
"""Check 8 del gate: smoke test end-to-end contra el servidor ya levantado.

Manda cada fixture a POST /move y exige:
  1. respuesta 200 con un movimiento de los cuatro literales;
  2. movimiento LEGAL, calculado aqui con una implementacion independiente de la del
     motor (si ambas coincidieran por compartir codigo, el check no probaria nada);
  3. p99 y maximo dentro del presupuesto de latencia, leido de config/loop.json.

El **arranque en frio** se mide aparte y con su propio umbral: la primera peticion
despues de levantar el proceso paga la carga del config y los primeros fallos de pagina,
y mezclarla con las demas convierte el maximo en un numero que no distingue un cerebro
lento de un proceso recien nacido. Se miden las dos cosas, no una menos.

La legalidad se define igual que en docs/rules.md#r-04: una casilla de cola solo queda
libre si los dos ultimos segmentos NO estan apilados.
"""
import argparse
import json
import pathlib
import sys
import time
import urllib.error
import urllib.request

DIRECTIONS = {"up": (0, 1), "down": (0, -1), "left": (-1, 0), "right": (1, 0)}


def blocked_cells(board):
    blocked = set()
    for snake in board["snakes"]:
        body = [(p["x"], p["y"]) for p in snake["body"]]
        stacked_tail = len(body) >= 2 and body[-1] == body[-2]
        last = len(body) - 1
        for i, cell in enumerate(body):
            if i == last and not stacked_tail:
                continue
            blocked.add(cell)
    return blocked


def legal_moves(request):
    board = request["board"]
    me = None
    for snake in board["snakes"]:
        if snake["id"] == request["you"]["id"]:
            me = snake
    if me is None:
        return set()
    head = (me["body"][0]["x"], me["body"][0]["y"])
    blocked = blocked_cells(board)
    legal = set()
    for name, (dx, dy) in DIRECTIONS.items():
        target = (head[0] + dx, head[1] + dy)
        if not (0 <= target[0] < board["width"] and 0 <= target[1] < board["height"]):
            continue
        if target in blocked:
            continue
        legal.add(name)
    return legal


def post(url, payload, timeout):
    data = json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    started = time.perf_counter()
    with urllib.request.urlopen(req, timeout=timeout) as response:
        body = json.loads(response.read().decode())
    return body, (time.perf_counter() - started) * 1000.0


# Payloads que el arbitro nunca mandaria, pero que un puerto abierto a internet si recibe.
# Cada uno tiene que salir 200 con un movimiento de las cuatro literales: INV-12 dice que
# /move NUNCA devuelve 5xx, y sin esto esa garantia dependia de que el parser fuera
# cuidadoso en cada rama, no de una comprobacion. ver docs/invariants.md#inv-12
PAYLOADS_ADVERSOS = [
    ("json invalido", "no es json"),
    ("cuerpo vacio", ""),
    ("json que no es objeto", "[1,2,3]"),
    ("game con el tipo equivocado", '{"game":5}'),
    ("ruleset con el tipo equivocado", '{"game":{"ruleset":3}}'),
    ("board que no es objeto", '{"board":"no-es-objeto"}'),
    ("turn como string", '{"turn":"dos","board":{"width":11,"height":11,'
                         '"snakes":[],"food":[],"hazards":[]}}'),
    ("body como string", '{"board":{"width":11,"height":11,"snakes":[{"id":"a",'
                         '"body":"no"}],"food":[],"hazards":[]},"you":{"id":"a"}}'),
    ("body vacio", '{"board":{"width":11,"height":11,"snakes":[{"id":"a","body":[]}],'
                   '"food":[],"hazards":[]},"you":{"id":"a"}}'),
    ("coordenadas fuera de rango", '{"board":{"width":11,"height":11,"snakes":[{"id":"a",'
                                   '"body":[{"x":99999999999,"y":-88888888}]}],"food":[],'
                                   '"hazards":[]},"you":{"id":"a"}}'),
    ("salud imposible", '{"board":{"width":11,"height":11,"snakes":[{"id":"a",'
                        '"body":[{"x":1,"y":1}],"health":99999}],"food":[],"hazards":[]},'
                        '"you":{"id":"a"},"turn":-5}'),
    ("tablero que el cerebro no instancia", '{"board":{"width":19,"height":19,"snakes":'
                                            '[{"id":"a","body":[{"x":1,"y":1}]}],"food":[],'
                                            '"hazards":[]},"you":{"id":"a"}}'),
    ("sin la serpiente propia", '{"board":{"width":11,"height":11,"snakes":[{"id":"b",'
                                '"body":[{"x":1,"y":1}]}],"food":[],"hazards":[]},'
                                '"you":{"id":"a"}}'),
    ("anidamiento profundo", "[" * 2000 + "]" * 2000),
]


def adversos(url):
    """Devuelve la lista de fallos; vacia si el servidor aguanta todo.

    Las tres rutas POST, no solo /move: `/start` devolvia 500 con cualquier JSON que no
    fuera un objeto, y ademas filtraba el mensaje de la excepcion en una cabecera. El
    check 8 no lo veia porque nunca le mandaba nada a esa ruta.
    """
    fallos = []
    for ruta in ("/move", "/start", "/end"):
        for nombre, cuerpo in PAYLOADS_ADVERSOS:
            etiqueta = f"adverso '{nombre}' en {ruta}"
            peticion = urllib.request.Request(
                f"{url}{ruta}", data=cuerpo.encode("utf-8"),
                headers={"Content-Type": "application/json"}, method="POST")
            try:
                with urllib.request.urlopen(peticion, timeout=5) as respuesta:
                    codigo = respuesta.status
                    datos = respuesta.read()
                    cabeceras = dict(respuesta.headers)
            except urllib.error.HTTPError as error:
                fallos.append(f"{etiqueta}: HTTP {error.code}, y estas rutas nunca son 5xx")
                continue
            except (urllib.error.URLError, OSError) as error:
                fallos.append(f"{etiqueta}: sin respuesta ({error})")
                continue
            if codigo != 200:
                fallos.append(f"{etiqueta}: HTTP {codigo}")
                continue
            # Un 500 de cpp-httplib viaja con el mensaje de la excepcion en una cabecera:
            # aunque el codigo fuera 200, filtrar internos es un fallo por si solo.
            for clave in cabeceras:
                if "EXCEPTION" in clave.upper():
                    fallos.append(f"{etiqueta}: filtra la excepcion en la cabecera {clave}")
            if ruta != "/move":
                continue
            try:
                movimiento = json.loads(datos).get("move")
            except ValueError:
                fallos.append(f"{etiqueta}: la respuesta no es JSON")
                continue
            if movimiento not in DIRECTIONS:
                fallos.append(f"{etiqueta}: movimiento invalido '{movimiento}'")
    return fallos


def conexiones_colgadas(url, cuantas=24):
    """Ocupa hilos con peticiones que anuncian cuerpo y no lo mandan.

    Con el pool por defecto de cpp-httplib y un read timeout de 5 s, ocho de estas
    dejaban sin respuesta a una peticion legitima durante segundos. No devolver nada es
    peor que un 5xx: el arbitro aplica su movimiento por defecto igualmente, pero encima
    se come el timeout entero.
    """
    import socket
    from urllib.parse import urlparse

    destino = urlparse(url)
    host = destino.hostname or "127.0.0.1"
    puerto = destino.port or 80
    abiertas = []
    try:
        for _ in range(cuantas):
            sock = socket.create_connection((host, puerto), timeout=2)
            sock.sendall(b"POST /move HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n\r\n")
            abiertas.append(sock)
        cuerpo = json.dumps({"board": {"width": 11, "height": 11, "snakes": [],
                                       "food": [], "hazards": []}}).encode("utf-8")
        peticion = urllib.request.Request(f"{url}/move", data=cuerpo,
                                          headers={"Content-Type": "application/json"},
                                          method="POST")
        inicio = time.monotonic()
        try:
            with urllib.request.urlopen(peticion, timeout=4) as respuesta:
                if respuesta.status != 200:
                    return [f"con {cuantas} conexiones colgadas: HTTP {respuesta.status}"]
        except (urllib.error.URLError, OSError) as error:
            return [f"con {cuantas} conexiones colgadas: sin respuesta ({error})"]
        transcurrido = (time.monotonic() - inicio) * 1000.0
        if transcurrido > 1000.0:
            return [f"con {cuantas} conexiones colgadas: respondio en {transcurrido:.0f} ms"]
    except OSError as error:
        return [f"no se pudieron abrir las conexiones colgadas ({error})"]
    finally:
        for sock in abiertas:
            sock.close()
    return []


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8080")
    parser.add_argument("--fixtures", default="tests/fixtures")
    parser.add_argument("--config", default="config/loop.json")
    parser.add_argument("--p99-ms", type=float, default=None)
    parser.add_argument("--max-ms", type=float, default=None)
    parser.add_argument("--repeats", type=int, default=20)
    parser.add_argument("--json-out", default="")
    args = parser.parse_args()

    # Los umbrales viven en config/loop.json, nunca en este script: el harness exige que
    # ningun umbral este hardcodeado (docs/harness.md#h-05).
    thresholds = json.loads(pathlib.Path(args.config).read_text(encoding="utf-8"))
    perf = thresholds["classes"]["perf"]["thresholds"]
    p99_budget = args.p99_ms if args.p99_ms is not None else float(perf["p99_move_ms_max"])
    max_budget = args.max_ms if args.max_ms is not None else float(perf["max_move_ms_max"])
    cold_budget = float(perf["cold_start_ms_max"])

    fixtures = sorted(pathlib.Path(args.fixtures).glob("*.json"))
    if len(fixtures) < 10:
        print(f"FAIL solo hay {len(fixtures)} fixtures, se exigen >= 10")
        return 1

    latencies = []
    failures = []

    # Calentamiento: una peticion real, con su movimiento comprobado igual que las demas,
    # cuya latencia se contabiliza como arranque en frio y NO entra en la muestra.
    cold_start = None
    first = json.loads(fixtures[0].read_text(encoding="utf-8"))
    try:
        body, cold_start = post(f"{args.url}/move", first, timeout=5)
        legal_first = legal_moves(first)
        move = body.get("move")
        if move not in DIRECTIONS:
            failures.append(f"{fixtures[0].name} (calentamiento): movimiento invalido '{move}'")
        elif legal_first and move not in legal_first:
            failures.append(
                f"{fixtures[0].name} (calentamiento): movimiento ILEGAL '{move}'")
    except (urllib.error.URLError, OSError) as exc:
        failures.append(f"{fixtures[0].name} (calentamiento): sin respuesta ({exc})")

    for path in fixtures:
        request = json.loads(path.read_text(encoding="utf-8"))
        legal = legal_moves(request)
        for _ in range(args.repeats):
            try:
                body, millis = post(f"{args.url}/move", request, timeout=5)
            except (urllib.error.URLError, OSError) as exc:
                failures.append(f"{path.name}: sin respuesta ({exc})")
                break
            latencies.append(millis)
            move = body.get("move")
            if move not in DIRECTIONS:
                failures.append(f"{path.name}: movimiento invalido '{move}'")
                break
            if legal and move not in legal:
                failures.append(
                    f"{path.name}: movimiento ILEGAL '{move}', legales {sorted(legal)}")
                break

    # El servidor sigue vivo tras los fixtures: ahora se le manda lo que no espera.
    fallos_adversos = adversos(args.url)
    failures.extend(fallos_adversos)
    fallos_adversos.extend(conexiones_colgadas(args.url))
    failures.extend(conexiones_colgadas(args.url))

    if not latencies:
        print("FAIL ninguna respuesta del servidor")
        return 1

    latencies.sort()
    p50 = latencies[len(latencies) // 2]
    p99 = latencies[min(len(latencies) - 1, int(len(latencies) * 0.99))]
    worst = latencies[-1]

    cold_txt = f"{cold_start:.2f}ms" if cold_start is not None else "no medido"
    total_adversos = len(PAYLOADS_ADVERSOS) * 3
    print(f"adversos={total_adversos} en /move,/start,/end  fallos={len(fallos_adversos)}")
    print(f"fixtures={len(fixtures)} peticiones={len(latencies)} "
          f"p50={p50:.2f}ms p99={p99:.2f}ms max={worst:.2f}ms "
          f"arranque_en_frio={cold_txt}")

    if args.json_out:
        pathlib.Path(args.json_out).write_text(json.dumps({
            "fixtures": len(fixtures),
            "requests": len(latencies),
            "p50_ms": round(p50, 3),
            "p99_ms": round(p99, 3),
            "max_ms": round(worst, 3),
            "cold_start_ms": round(cold_start, 3) if cold_start is not None else None,
            "illegal_moves": len(failures),
        }, indent=2) + "\n", encoding="utf-8")

    for failure in failures:
        print(f"FAIL {failure}")
    if failures:
        return 1
    if p99 > p99_budget:
        print(f"FAIL p99 {p99:.2f}ms > {p99_budget}ms")
        return 1
    if worst > max_budget:
        print(f"FAIL maximo {worst:.2f}ms > {max_budget}ms")
        return 1
    if cold_start is None:
        print("FAIL no se midio el arranque en frio")
        return 1
    if cold_start > cold_budget:
        print(f"FAIL arranque en frio {cold_start:.2f}ms > {cold_budget}ms")
        return 1
    print("OK   todos los movimientos legales y dentro del presupuesto")
    return 0


if __name__ == "__main__":
    sys.exit(main())
