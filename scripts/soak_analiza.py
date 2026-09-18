#!/usr/bin/env python3
"""Analisis del soak: separa lo que midio el arbitro de lo que costo nuestro codigo.

Vive aparte de soak.sh para poder re-analizar una corrida ya guardada sin repetirla:

    python3 scripts/soak_analiza.py docs/results/soak-<fecha> 500
"""
import json, sys, statistics, pathlib, re

raiz = pathlib.Path(sys.argv[1])
timeout = float(sys.argv[2])

# 1. Lo que midio el arbitro. `latency` de un turno es la respuesta del turno anterior,
#    en milisegundos enteros; el turno 0 no trae medida previa y se descarta.
arbitro, primeras, timeouts = [], [], 0
for partida in sorted(raiz.glob("g*.jsonl")):
    lineas = [l for l in partida.read_text(encoding="utf-8").splitlines() if l.strip()]
    vistas = 0
    for linea in lineas[1:-1]:
        try:
            estado = json.loads(linea)
        except ValueError:
            continue
        if estado.get("turn", 0) == 0:
            continue
        for snake in estado.get("board", {}).get("snakes", []):
            if snake.get("name") != "v0-baseline":
                continue
            try:
                ms = float(snake.get("latency", ""))
            except ValueError:
                continue
            arbitro.append(ms)
            if ms >= timeout:
                timeouts += 1
            vistas += 1
            if vistas == 1:
                primeras.append(ms)

# 2. Lo que costo nuestro codigo, con resolucion de microsegundos.
interno = [float(m.group(1)) / 1000.0
           for m in re.finditer(r"\bus=(\d+)", (raiz / "server.log").read_text(
               encoding="utf-8", errors="replace"))]

def pct(v, p):
    if not v:
        return 0.0
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))]

def bloque(nombre, v, unidad):
    if not v:
        return {"metrica": nombre, "muestras": 0}
    return {"metrica": nombre, "unidad": unidad, "muestras": len(v),
            "p50_ms": round(pct(v, 0.50), 3), "p95_ms": round(pct(v, 0.95), 3),
            "p99_ms": round(pct(v, 0.99), 3), "max_ms": round(max(v), 3),
            "media_ms": round(statistics.fmean(v), 3)}

resumen = {
    "partidas": len(list(raiz.glob("g*.jsonl"))),
    "timeouts": timeouts,
    "timeout_ms": timeout,
    "latencia_arbitro": bloque("ida y vuelta medida por el arbitro", arbitro,
                               "ms enteros, truncados por .Milliseconds()"),
    "computo_interno": bloque("tiempo dentro de decide()", interno, "ms con resolucion de us"),
    "arranque_en_frio": bloque("primera medida de cada partida", primeras, "ms enteros"),
}
(raiz / "resumen.json").write_text(json.dumps(resumen, indent=2, ensure_ascii=False) + "\n",
                                  encoding="utf-8")

print()
print(f"partidas={resumen['partidas']}  timeouts={timeouts}  (timeout={timeout:.0f} ms)")
for clave in ("latencia_arbitro", "computo_interno", "arranque_en_frio"):
    b = resumen[clave]
    if b["muestras"]:
        print(f"{clave:18} n={b['muestras']:<6} p50={b['p50_ms']:<8} p95={b['p95_ms']:<8} "
              f"p99={b['p99_ms']:<8} max={b['max_ms']}")
print()
print("OK   0 timeouts" if timeouts == 0 else f"FAIL {timeouts} timeouts")
sys.exit(0 if timeouts == 0 else 1)
