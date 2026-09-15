#!/usr/bin/env python3
"""Check 8 del gate: smoke test end-to-end contra el servidor ya levantado.

Manda cada fixture a POST /move y exige:
  1. respuesta 200 con un movimiento de los cuatro literales;
  2. movimiento LEGAL, calculado aqui con una implementacion independiente de la del
     motor (si ambas coincidieran por compartir codigo, el check no probaria nada);
  3. p99 y maximo dentro del presupuesto de latencia, leido de config/loop.json.

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

    fixtures = sorted(pathlib.Path(args.fixtures).glob("*.json"))
    if len(fixtures) < 10:
        print(f"FAIL solo hay {len(fixtures)} fixtures, se exigen >= 10")
        return 1

    latencies = []
    failures = []
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

    if not latencies:
        print("FAIL ninguna respuesta del servidor")
        return 1

    latencies.sort()
    p50 = latencies[len(latencies) // 2]
    p99 = latencies[min(len(latencies) - 1, int(len(latencies) * 0.99))]
    worst = latencies[-1]

    print(f"fixtures={len(fixtures)} peticiones={len(latencies)} "
          f"p50={p50:.2f}ms p99={p99:.2f}ms max={worst:.2f}ms")

    if args.json_out:
        pathlib.Path(args.json_out).write_text(json.dumps({
            "fixtures": len(fixtures),
            "requests": len(latencies),
            "p50_ms": round(p50, 3),
            "p99_ms": round(p99, 3),
            "max_ms": round(worst, 3),
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
    print("OK   todos los movimientos legales y dentro del presupuesto")
    return 0


if __name__ == "__main__":
    sys.exit(main())
