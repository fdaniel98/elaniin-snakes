#!/usr/bin/env python3
"""Prueba de mutacion del orquestador: las metricas `mutants` y `mutants_killed_ratio`.

    python3 scripts/mutants_tr.py [--json]

Una bateria de tests que pasa no dice si sirve. Esto le mete al codigo defectos concretos
-los que de verdad se cometen: un indice por otro, una guarda de menos, un contador que no
puede quedarse a cero- y comprueba que ALGUN test los mata. Un mutante que sobrevive es un
agujero en la bateria, no una curiosidad.

El oraculo son `training-room/test_tr.py` y `scripts/fuzz_tr.py` con pocos estados: entre
los dos cubren derivacion, persistencia y parsers.

Cada mutante restaura el fichero con `mv` MAS `touch`. La fase 0 aprendio por las malas que
`mv` a secas conserva la fecha anterior, y con eso una mutacion se quedaba dentro del
binario y mataba a los mutantes siguientes por ella; aqui es Python y no hay binario, pero
la restauracion se verifica igual comparando el contenido.
"""
import argparse
import json
import pathlib
import subprocess
import sys

RAIZ = pathlib.Path(__file__).resolve().parent.parent

# (id, fichero, texto original, texto mutado, que defecto simula)
MUTANTES = [
    ("m1", "training-room/tr.py",
     "                    orden.index(slug),",
     "                    0,",
     "el asiento es siempre 0: la rotacion deja de registrarse"),
    ("m2", "training-room/tr.py",
     "        promedio = sum(range(i + 1, j + 2)) / (j - i + 1)",
     "        promedio = i + 1",
     "los empates se desempatan por orden de aparicion"),
    ("m3", "training-room/tr.py",
     "        board = estado.get(\"board\")\n        if not isinstance(board, dict):\n            continue",
     "        board = estado.get(\"board\")",
     "se quita la guarda de tipo sobre board"),
    ("m4", "training-room/tr.py",
     "        if f\"{url}/end\" in linea:\n            continue",
     "        if False:\n            continue",
     "los fallos de /end cuentan como incidencias"),
    ("m5", "training-room/tr.py",
     "            modificadas += cur.rowcount",
     "            modificadas += 1",
     "el contador suma vueltas del bucle, no filas modificadas"),
    ("m6", "training-room/tr.py",
     "        for _sid, url, nombre in LINEA.findall(texto):",
     "        for _sid, nombre, url in LINEA.findall(texto):",
     "los grupos de la expresion regular cruzados"),
    ("m7", "training-room/tr.py",
     "    elif total > presupuesto:",
     "    elif total > 1000000:",
     "el reparto de CPU acepta cualquier torneo"),
    ("m8", "training-room/reporte.py",
     "            if total_fila != d[\"partidas_ok\"]:",
     "            if False:",
     "el descuadre de una fila deja de denunciarse"),
    ("m9", "training-room/reporte.py",
     "              f\" | {total_fila} |\")",
     "              f\" | 0 |\")",
     "el total de cada fila se publica como cero"),
    ("m10", "training-room/tr.py",
     "    if not ultimo_turno:\n        return None",
     "    if False:\n        return None",
     "un JSONL sin ninguna serpiente devuelve un resultado vacio en vez de None"),
]


def oraculo():
    """Los tests mas un fuzz corto. Devuelve True si TODO pasa."""
    for cmd in (["python3", "training-room/test_tr.py"],
                ["python3", "scripts/fuzz_tr.py", "--estados", "400"]):
        if subprocess.run(cmd, cwd=RAIZ, capture_output=True).returncode != 0:
            return False
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if not oraculo():
        print("ERROR el oraculo falla SIN mutar: arregla eso antes de medir nada",
              file=sys.stderr)
        return 2

    resultados = []
    for ident, fichero, viejo, nuevo, descripcion in MUTANTES:
        ruta = RAIZ / fichero
        original = ruta.read_text(encoding="utf-8")
        if viejo not in original:
            resultados.append({"id": ident, "estado": "ARNES_ROTO",
                               "motivo": "el texto a mutar ya no existe en el fichero",
                               "descripcion": descripcion})
            if not args.json:
                print(f"ARNES_ROTO {ident}: el texto a mutar ya no existe")
            continue
        ruta.write_text(original.replace(viejo, nuevo, 1), encoding="utf-8")
        try:
            vivo = oraculo()
        finally:
            ruta.write_text(original, encoding="utf-8")
            assert ruta.read_text(encoding="utf-8") == original, "la restauracion fallo"
        estado = "SOBREVIVE" if vivo else "MUERTO"
        resultados.append({"id": ident, "estado": estado, "descripcion": descripcion})
        if not args.json:
            print(f"{estado:<11} {ident}: {descripcion}")

    total = sum(1 for r in resultados if r["estado"] != "ARNES_ROTO")
    muertos = sum(1 for r in resultados if r["estado"] == "MUERTO")
    ratio = muertos / total if total else 0.0
    salida = {"mutants": total, "muertos": muertos,
              "mutants_killed_ratio": round(ratio, 4),
              "supervivientes": [r["id"] for r in resultados if r["estado"] == "SOBREVIVE"],
              "arnes_roto": [r["id"] for r in resultados if r["estado"] == "ARNES_ROTO"],
              "detalle": resultados}
    if args.json:
        print(json.dumps(salida, indent=2, ensure_ascii=False))
    else:
        print(f"\nmutantes {total}, muertos {muertos}, ratio {ratio:.4f}")
    # El umbral vive en config/loop.json y no se duplica aqui: este script MIDE, y quien
    # decide si el numero basta es el check 9.
    return 0 if not salida["arnes_roto"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
