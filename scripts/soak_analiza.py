#!/usr/bin/env python3
"""Analisis del soak: cuenta timeouts donde se pueden contar y mide donde se puede medir.

    python3 scripts/soak_analiza.py <dir> <timeout_ms> <partidas_esperadas>

Tres cosas que este script hace a proposito, porque la primera version las hacia mal:

1. **Los timeouts salen del stderr del arbitro, no del JSONL.** El JSONL no puede
   contarlos: la serpiente eliminada desaparece de `board.snakes`
   (`cli/commands/play.go:818-820`), asi que el timeout del turno que la mata no se
   exporta, y el ultimo turno de toda partida tampoco, porque el bucle hace `break` antes
   de exportar (`cli/commands/play.go:273-276`). Contar timeouts ahi perdia justo los que
   importan.
2. **Falla si no midio lo que decia.** Si faltan partidas, si el arbitro salio con error o
   si hay menos muestras de las exigidas, el veredicto es FAIL. Un soak que no jugo nada
   decia "0 timeouts" y salia 0.
3. **Casa por ID de serpiente, no por nombre.** El arbitro imprime `Snake ID: <uuid>
   URL: <url>` al arrancar; de ahi sale cual es la nuestra. Con el nombre hardcodeado, un
   cambio de nombre daba cero muestras y veredicto verde.
"""

import json
import pathlib
import re
import statistics
import sys

# Lo que el arbitro escribe cuando una snake no le sirve. Cada patron lleva su etiqueta.
PATRONES = (
    ("timeout", re.compile(r"context deadline exceeded")),
    ("conexion", re.compile(r"Request to \S+ failed")),
    ("status", re.compile(r"Got non-ok status code")),
    ("json", re.compile(r"Failed to (?:decode JSON|parse JSON|read response body)")),
    ("movimiento", re.compile(r"invalid move")),
)
ID_URL = re.compile(r"Snake ID:\s+(\S+)\s+URL:\s+(\S+?),")


def percentil(valores, p):
    if not valores:
        return 0.0
    ordenados = sorted(valores)
    return ordenados[min(len(ordenados) - 1, int(len(ordenados) * p))]


def bloque(nombre, valores, unidad):
    if not valores:
        return {"metrica": nombre, "muestras": 0}
    return {"metrica": nombre, "unidad": unidad, "muestras": len(valores),
            "p50_ms": round(percentil(valores, 0.50), 3),
            "p95_ms": round(percentil(valores, 0.95), 3),
            "p99_ms": round(percentil(valores, 0.99), 3),
            "max_ms": round(max(valores), 3),
            "media_ms": round(statistics.fmean(valores), 3)}


def main():
    raiz = pathlib.Path(sys.argv[1])
    timeout = float(sys.argv[2])
    esperadas = int(sys.argv[3]) if len(sys.argv) > 3 else 0

    fallos = []
    manifiesto = raiz / "partidas.jsonl"
    if not manifiesto.exists():
        print(f"FAIL no hay {manifiesto}: el soak no llego a jugar nada")
        return 1

    partidas = [json.loads(l) for l in manifiesto.read_text(encoding="utf-8").splitlines()
                if l.strip()]
    if esperadas and len(partidas) != esperadas:
        fallos.append(f"se pidieron {esperadas} partidas y se jugaron {len(partidas)}")
    con_error = [p["id"] for p in partidas if p.get("rc", 1) != 0]
    if con_error:
        fallos.append(f"{len(con_error)} partidas con el arbitro en error: {con_error[:5]}")

    # 1. Quejas del arbitro sobre NUESTRA url, por partida.
    incidencias = {etiqueta: 0 for etiqueta, _ in PATRONES}
    detalle = []
    for partida in partidas:
        log = raiz / f"{partida['id']}.ref.log"
        if not log.exists():
            fallos.append(f"{partida['id']}: falta el log del arbitro")
            continue
        texto = log.read_text(encoding="utf-8", errors="replace")
        nuestra_url = partida["url"]
        for linea in texto.splitlines():
            if nuestra_url not in linea:
                continue
            for etiqueta, patron in PATRONES:
                if patron.search(linea):
                    incidencias[etiqueta] += 1
                    detalle.append(f"{partida['id']}: {linea.strip()[:160]}")
        # El bloque de error del timeout va en la linea siguiente a "Request to ... failed",
        # que si lleva la url; contarla dos veces inflaria, asi que `conexion` y `timeout`
        # se reportan por separado y el veredicto usa el maximo, no la suma.
    timeouts = max(incidencias["timeout"], incidencias["conexion"])
    respuestas_malas = incidencias["status"] + incidencias["json"] + incidencias["movimiento"]

    # 2. Distribucion de latencia desde el JSONL, casando por ID.
    latencias, primeras = [], []
    for partida in partidas:
        jsonl = raiz / f"{partida['id']}.jsonl"
        log = raiz / f"{partida['id']}.ref.log"
        if not jsonl.exists():
            fallos.append(f"{partida['id']}: falta el JSONL")
            continue
        nuestro_id = None
        if log.exists():
            for sid, url in ID_URL.findall(log.read_text(encoding="utf-8", errors="replace")):
                if url == partida["url"]:
                    nuestro_id = sid
        if nuestro_id is None:
            fallos.append(f"{partida['id']}: el log del arbitro no dice el id de nuestra snake")
            continue
        vistas = 0
        lineas = [l for l in jsonl.read_text(encoding="utf-8").splitlines() if l.strip()]
        for linea in lineas[1:-1]:
            try:
                estado = json.loads(linea)
            except ValueError:
                fallos.append(f"{partida['id']}: linea del JSONL ilegible")
                continue
            if estado.get("turn", 0) == 0:
                continue
            for snake in estado.get("board", {}).get("snakes", []):
                if snake.get("id") != nuestro_id:
                    continue
                try:
                    ms = float(snake.get("latency", ""))
                except ValueError:
                    continue
                latencias.append(ms)
                vistas += 1
                if vistas == 1:
                    primeras.append(ms)

    # 3. Computo interno, del log del servidor.
    log_servidor = raiz / "server.log"
    interno = []
    if log_servidor.exists():
        interno = [float(m.group(1)) / 1000.0
                   for m in re.finditer(r"\bus=(\d+)",
                                        log_servidor.read_text(encoding="utf-8",
                                                               errors="replace"))]
    else:
        fallos.append("falta server.log: no hay medida del computo interno")

    # Un soak que no midio nada no puede salir verde.
    minimo = max(20, len(partidas) * 5)
    if len(interno) < minimo:
        fallos.append(f"solo {len(interno)} movimientos medidos, se exigen >= {minimo}")

    resumen = {
        "partidas_pedidas": esperadas,
        "partidas_jugadas": len(partidas),
        "timeout_ms": timeout,
        "timeouts": timeouts,
        "respuestas_rechazadas": respuestas_malas,
        "incidencias_por_tipo": incidencias,
        "latencia_arbitro": bloque("ida y vuelta medida por el arbitro", latencias,
                                   "ms enteros, truncados"),
        "computo_interno": bloque("tiempo dentro de decide()", interno,
                                  "ms con resolucion de us"),
        "arranque_en_frio": bloque("primera medida de cada partida", primeras, "ms enteros"),
        "fallos": fallos,
    }
    (raiz / "resumen.json").write_text(
        json.dumps(resumen, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print()
    print(f"partidas={len(partidas)}/{esperadas or len(partidas)}  "
          f"timeouts={timeouts}  respuestas_rechazadas={respuestas_malas}  "
          f"(timeout={timeout:.0f} ms)")
    for clave in ("latencia_arbitro", "computo_interno", "arranque_en_frio"):
        b = resumen[clave]
        if b["muestras"]:
            print(f"{clave:18} n={b['muestras']:<6} p50={b['p50_ms']:<8} "
                  f"p95={b['p95_ms']:<8} p99={b['p99_ms']:<8} max={b['max_ms']}")
    for linea in detalle[:10]:
        print(f"  {linea}")
    print()

    if timeouts:
        fallos.append(f"{timeouts} timeouts")
    if respuestas_malas:
        fallos.append(f"{respuestas_malas} respuestas que el arbitro rechazo")
    if fallos:
        for f in fallos:
            print(f"FAIL {f}")
        return 1
    print("OK   0 timeouts, 0 respuestas rechazadas, y las partidas pedidas jugadas")
    return 0


if __name__ == "__main__":
    sys.exit(main())
