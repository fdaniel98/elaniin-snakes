#!/usr/bin/env python3
"""Genera el reporte de un torneo: markdown canonico y HTML con tablas y un grafico.

    python3 training-room/reporte.py --out docs/results/torneo-v1 [--causas build/release/bin/causas]

El markdown es el documento; el HTML es el mismo contenido para mirarlo. Ninguno de los
dos calcula nada que no salga de `torneo.sqlite` y de los logs del arbitro que hay al lado.

Lo que este reporte NO hace, a proposito:

  - no da p-valores ni veredictos. La metrica primaria con test estadistico es la
    diferencia pareada de posicion media por bloque, y eso es el A/B de la fase 4. Mirar
    diez metricas descriptivas a alfa 0.05 da un 40% de probabilidad de un falso positivo,
    asi que aqui se describen y ya;
  - no rellena lo que no midio. Una causa de muerte ambigua se cuenta como ambigua.
"""
import argparse
import json
import sqlite3
import subprocess
import sys
from collections import Counter
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
# Slots 1-4 de la paleta categorica, validados contra las dos superficies.
COLORES = [("#2a78d6", "#3987e5"), ("#eb6834", "#d95926"),
           ("#1baf7a", "#199e70"), ("#eda100", "#c98500")]


def consulta(db, sql, *args):
    return db.execute(sql, args).fetchall()


def recoge(salida, binario_causas):
    db = sqlite3.connect(salida / "torneo.sqlite")
    d = {}
    d["partidas_ok"] = consulta(db, "SELECT COUNT(*) FROM partidas WHERE arbitro_rc=0")[0][0]
    d["partidas_error"] = consulta(db, "SELECT COUNT(*) FROM partidas WHERE arbitro_rc!=0")[0][0]
    fila = consulta(db, "SELECT topologia, gauntlet FROM partidas LIMIT 1")
    d["topologia"] = json.loads(fila[0][0]) if fila else {}
    d["gauntlet"] = fila[0][1] if fila else "?"
    d["commit"] = (consulta(db, "SELECT commit_snake FROM participantes WHERE commit_snake IS NOT NULL LIMIT 1")
                   or [[None]])[0][0]
    d["hash_config"] = (consulta(db, "SELECT hash_config FROM participantes WHERE hash_config IS NOT NULL LIMIT 1")
                        or [[None]])[0][0]
    d["semillas"] = consulta(db, "SELECT COUNT(DISTINCT semilla) FROM partidas WHERE arbitro_rc=0")[0][0]
    d["turnos"] = consulta(db, "SELECT AVG(turnos), MIN(turnos), MAX(turnos) FROM partidas WHERE arbitro_rc=0")[0]

    d["clasificacion"] = consulta(db, """
        SELECT p.slug, AVG(p.puesto), SUM(CASE WHEN p.puesto=1.0 THEN 1 ELSE 0 END),
               COUNT(*), AVG(p.turnos_sobrevividos)
        FROM participantes p JOIN partidas g ON g.id=p.partida_id
        WHERE g.arbitro_rc=0 GROUP BY p.slug ORDER BY 2""")
    # El "asiento" es el orden de los argumentos del arbitro, y NO decide donde sale cada
    # serpiente: el arbitro recorre un `map[string]SnakeState` de Go, cuyo orden de
    # iteracion esta aleatorizado por especificacion (`cli/commands/play.go:349-352`), y
    # ademas baraja los puntos de salida (`board.go:200-223`). Comprobado sobre estas
    # mismas partidas: cada posicion de argumento acabo en 8 puntos de salida distintos.
    # Agrupar por asiento era un test que no podia fallar. Se agrupa por el punto de
    # salida REAL, que se lee del turno 0 de cada JSONL.
    salidas = {}
    for pid, in db.execute("SELECT id FROM partidas WHERE arbitro_rc=0"):
        jsonl = salida / f"{pid}.jsonl"
        if not jsonl.exists() or jsonl.stat().st_size == 0:
            continue
        lineas = [l for l in jsonl.read_text(encoding="utf-8", errors="replace").splitlines()
                  if l.strip()]
        if len(lineas) < 2:
            continue
        try:
            turno0 = json.loads(lineas[1])
        except ValueError:
            continue
        for sn in turno0.get("board", {}).get("snakes", []):
            cabeza = sn.get("head", {})
            salidas[(pid, sn.get("name"))] = (cabeza.get("x"), cabeza.get("y"))
    puestos = {(r[0], r[1]): r[2] for r in db.execute(
        "SELECT partida_id, slug, puesto FROM participantes")}
    por_salida = {}
    for clave, pos in salidas.items():
        pu = puestos.get(clave)
        if pu is None:
            continue
        por_salida.setdefault(pos, []).append((clave[1], pu))
    d["salidas"] = por_salida
    d["latencias"] = consulta(db, """
        SELECT l.slug, AVG(l.p50), AVG(l.p95), AVG(l.p99), MAX(l.maximo),
               SUM(l.timeouts), SUM(l.movimientos)
        FROM latencias l JOIN partidas g ON g.id=l.partida_id
        WHERE g.arbitro_rc=0 GROUP BY l.slug ORDER BY 4 DESC""")

    # Causas de muerte: las pide al motor verificado, partida a partida.
    d["causas"] = None
    if binario_causas and Path(binario_causas).exists():
        # Se agrega por NOMBRE, no por id: el id cambia en cada partida, y lo que hace
        # accionable esta tabla es saber de quien es cada muerte. Las nuestras salen
        # determinadas porque `causas --nuestra` le pregunta al cerebro en vez de enumerar.
        por_nombre = {}
        for jsonl in sorted(salida.glob("g*.jsonl")):
            if jsonl.stat().st_size == 0:
                continue
            r = subprocess.run([binario_causas, str(jsonl), "--nuestra", "v0-baseline"],
                               capture_output=True, text=True)
            if r.returncode != 0:
                continue
            for reg in json.loads(r.stdout)["snakes"].values():
                por_nombre.setdefault(reg.get("nombre") or "?", Counter())[reg["causa"]] += 1
        d["causas"] = por_nombre
    return d


def md(d):
    t = d["topologia"]
    l = []
    a = l.append
    a("---")
    a('title: "Torneo v0-baseline contra gauntlet-v1"')
    a('read_when: "antes de comparar una version nueva contra la linea base del campo congelado"')
    a("authority: canonical")
    a(f"last_verified: {__import__('time').strftime('%Y-%m-%d')}")
    a("---")
    a("")
    a("")
    a("## T-01 Que se midio {#t-01}")
    a("")
    a("| campo | valor |")
    a("|---|---|")
    a(f"| campo congelado | `{d['gauntlet']}` |")
    a(f"| partidas jugadas / con el arbitro en error | {d['partidas_ok']} / {d['partidas_error']} |")
    a(f"| bloques (semillas distintas) | {d['semillas']} |")
    a(f"| commit de nuestra snake | `{d['commit']}` |")
    a(f"| hash del config | `{d['hash_config']}` |")
    a(f"| turnos por partida (media / min / max) | {d['turnos'][0]:.1f} / {d['turnos'][1]} / {d['turnos'][2]} |")
    a(f"| nucleos / hilos por nucleo / governor | {t.get('nucleos','?')} / "
      f"{t.get('hilos_por_nucleo','?')} / {t.get('gobernador','?')} |")
    a("")
    a("Dos corridas con nucleos, hilos por nucleo o governor distintos **no se comparan**.")
    a("El governor sale `desconocido` en WSL2 porque no expone `cpufreq`: la frecuencia la")
    a("gobierna el anfitrion y no se puede ni leer ni fijar desde aqui.")
    a("")
    a("## T-02 Clasificacion {#t-02}")
    a("")
    a("| snake | puesto medio | primeros | partidas | turnos vividos |")
    a("|---|---|---|---|---|")
    for slug, puesto, primeros, n, turnos in d["clasificacion"]:
        a(f"| {slug} | {puesto:.3f} | {primeros} | {n} | {turnos:.1f} |")
    a("")
    a("Puesto medio con **rango compartido promediado** para las eliminadas en el mismo")
    a("turno, nunca por indice de asiento (ver docs/rules.md#r-12).")
    a("")
    a("## T-03 Efecto del punto de salida {#t-03}")
    a("")
    a("Si una casilla de salida favoreciera, el puesto medio de cualquier snake saliendo")
    a("de ella se separaria de 2.5. Si estas filas son planas, la posicion inicial no esta")
    a("contaminando la comparacion; si no lo son, el resto del reporte no vale.")
    a("")
    a("**No se agrupa por el orden de los argumentos.** Ese orden no decide donde sale")
    a("nadie: el arbitro recorre un mapa de Go, cuyo orden de iteracion esta aleatorizado")
    a("por especificacion (`cli/commands/play.go:349-352`), y ademas baraja los puntos de")
    a("salida (`board.go:200-223`). Agrupar por asiento era un test que no podia fallar.")
    a("")
    if not d.get("salidas"):
        # Una tabla vacia sin decir por que es una perdida silenciosa, que es justo lo que
        # este reporte no puede permitirse.
        a("**Sin datos:** no hay JSONL junto a la base, asi que no se pueden leer los")
        a("puntos de salida. La comparacion de arriba queda SIN esta comprobacion.")
        a("")
    a("| punto de salida | puesto medio (cualquiera) | n | puesto medio nuestro | n |")
    a("|---|---|---|---|---|")
    for pos in sorted(d.get("salidas", {})):
        filas = d["salidas"][pos]
        todos = [pu for _slug, pu in filas]
        nuestras = [pu for slug, pu in filas if slug == "v0-baseline"]
        media_n = f"{sum(nuestras) / len(nuestras):.3f}" if nuestras else "-"
        a(f"| ({pos[0]}, {pos[1]}) | {sum(todos) / len(todos):.3f} | {len(todos)} | "
          f"{media_n} | {len(nuestras)} |")
    a("")
    a("## T-04 Latencia y timeouts {#t-04}")
    a("")
    a("| snake | p50 | p95 | p99 | maximo | timeouts | movimientos | tasa |")
    a("|---|---|---|---|---|---|---|---|")
    for slug, p50, p95, p99, mx, to, movs in d["latencias"]:
        tasa = (to / movs * 100) if (to is not None and movs) else 0.0
        a(f"| {slug} | {p50:.1f} | {p95:.1f} | {p99:.1f} | {mx:.0f} | {to} | {movs} | {tasa:.3f} % |")
    a("")
    a("Milisegundos, medidos **por el arbitro**, que es quien decide si una respuesta llego")
    a("a tiempo. Los timeouts salen de su stderr y no del JSONL: la serpiente eliminada")
    a("desaparece del turno siguiente y el ultimo turno no se exporta, asi que contarlos en")
    a("el JSONL perderia justo los que importan (ver docs/rules-parametros.md#r-20).")
    a("Los fallos de `/end` **no** se cuentan: no cuestan un movimiento.")
    a("")
    if d["causas"]:
        # TODAS las categorias son columnas, incluidas `modelo_discrepa` y
        # `final_no_exportado`. La primera version solo listaba las causas "de verdad" mas
        # `ambigua` y `sobrevivio`, y una fila con cualquier otra cosa desaparecia de la
        # tabla sin dejar rastro: nuestras filas sumaban 199 de 200. Una tabla de la que se
        # cae una fila miente aunque cada celda sea correcta.
        orden = ("cabezazo", "cuerpo_propio", "cuerpo_rival", "pared", "hambre", "hazard",
                 "ambigua", "modelo_discrepa", "sin_candidato", "final_no_exportado",
                 "sobrevivio")
        vistas = {c for cont in d["causas"].values() for c in cont}
        columnas = [c for c in orden if c in vistas] + sorted(vistas - set(orden))
        a("## T-05 Causas de muerte {#t-05}")
        a("")
        a("| snake | " + " | ".join(columnas) + " | total |")
        a("|---" * (len(columnas) + 2) + "|")
        descuadre = []
        for nombre in sorted(d["causas"]):
            cont = d["causas"][nombre]
            total_fila = sum(cont.values())
            if total_fila != d["partidas_ok"]:
                descuadre.append((nombre, total_fila))
            a(f"| {nombre} | " + " | ".join(str(cont.get(c, 0)) for c in columnas) +
              f" | {total_fila} |")
        a("")
        if descuadre:
            # No se esconde: si una snake no aparece en todas las partidas, el que lea la
            # tabla tiene que saberlo antes de sacar porcentajes de ella.
            a(f"**AVISO** estas filas no suman las {d['partidas_ok']} partidas: " +
              ", ".join(f"{n} ({t})" for n, t in descuadre) + ".")
            a("")
        nuestras = d["causas"].get("v0-baseline", Counter())
        muertes = sum(v for k, v in nuestras.items() if k != "sobrevivio")
        sin_determinar = sum(nuestras.get(k, 0) for k in
                             ("ambigua", "modelo_discrepa", "sin_candidato", "final_no_exportado"))
        a(f"**De nuestras {muertes} muertes, {muertes - sin_determinar} estan determinadas.**")
        a("El JSONL no exporta los movimientos, asi que el de una serpiente que muere se")
        a("enumera y se queda con los candidatos que reproducen el turno siguiente")
        a("observado; cuando varios llevan a causas distintas, es `ambigua` y se cuenta como")
        a("tal. Para la nuestra hay atajo: el cerebro es determinista, asi que se le")
        a("pregunta. El movimiento modelado tiene que estar entre los candidatos")
        a("consistentes o la fila sale `modelo_discrepa`, que seria un hallazgo -el replay")
        a("creyendo que hicimos algo que no hicimos- y no un detalle a tapar.")
        a("")
        a("Las de los rivales siguen siendo ambiguas en su mayoria y asi se quedan: no")
        a("tenemos su cerebro, y elegir la causa mas probable seria inventar un dato.")
        a("")
    a("## T-06 Lo que este reporte no dice {#t-06}")
    a("")
    a("- **No hay veredicto ni p-valores.** Todo lo de arriba es descriptivo. La metrica")
    a("  con test es la diferencia pareada de posicion media por bloque, y ese es el A/B de")
    a("  la fase 4.")
    a("- **El campo mide menos de lo que parece.** Las tres rivales salen del mismo binario,")
    a("  asi que sus errores estan correlacionados.")
    a("- **No hay reproducibilidad bit a bit.** El arbitro oficial usa el `math/rand` de Go.")
    a("  Se persisten la semilla y el JSONL de cada partida y nada mas")
    a("  (ver docs/decisions/ADR-0016-reproducibilidad-de-la-arena.md#d-0151).")
    a("")
    return "\n".join(l) + "\n"


def html(d):
    clasif = d["clasificacion"]
    peor = max(p for _, p, *_ in clasif) if clasif else 4.0
    barras = []
    for i, (slug, puesto, primeros, n, _turnos) in enumerate(clasif):
        ancho = puesto / peor * 100
        claro, oscuro = COLORES[i % len(COLORES)]
        barras.append(
            f'<div class="fila"><div class="etiqueta">{slug}</div>'
            f'<div class="pista"><div class="barra" style="--c:{claro};--cd:{oscuro};'
            f'width:{ancho:.1f}%"></div>'
            f'<span class="valor">{puesto:.2f}</span></div>'
            f'<div class="nota">{primeros} de {n} ganadas</div></div>')
    filas_lat = "".join(
        f"<tr><td>{s}</td><td>{p50:.1f}</td><td>{p99:.1f}</td><td>{mx:.0f}</td>"
        f"<td>{to}</td><td>{movs}</td></tr>"
        for s, p50, _p95, p99, mx, to, movs in d["latencias"])
    t = d["topologia"]
    return f"""<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Torneo {d['gauntlet']}</title>
<style>
:root {{ color-scheme: light;
  --surface: #fcfcfb; --texto: #0b0b0b; --texto2: #52514e; --linea: #e3e2dd; }}
@media (prefers-color-scheme: dark) {{ :root:where(:not([data-theme="light"])) {{
  color-scheme: dark; --surface:#1a1a19; --texto:#fff; --texto2:#c3c2b7; --linea:#35342f; }} }}
:root[data-theme="dark"] {{ color-scheme: dark;
  --surface:#1a1a19; --texto:#fff; --texto2:#c3c2b7; --linea:#35342f; }}
body {{ background: var(--surface); color: var(--texto); margin: 0;
  font: 15px/1.55 ui-sans-serif, system-ui, sans-serif; }}
main {{ max-width: 860px; margin: 0 auto; padding: 32px 16px 64px; }}
h1 {{ font-size: 1.5rem; margin: 0 0 4px; }}
h2 {{ font-size: 1.05rem; margin: 40px 0 12px; }}
p.sub {{ color: var(--texto2); margin: 0 0 8px; }}
table {{ border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; }}
th, td {{ text-align: right; padding: 6px 8px; border-bottom: 1px solid var(--linea); }}
th:first-child, td:first-child {{ text-align: left; }}
th {{ color: var(--texto2); font-weight: 500; }}
.fila {{ display: grid; grid-template-columns: 9.5rem 1fr 8rem; gap: 10px;
  align-items: center; margin-bottom: 8px; }}
.pista {{ display: flex; align-items: center; gap: 8px; }}
.barra {{ height: 14px; background: var(--c); border-radius: 0 4px 4px 0; min-width: 2px; }}
@media (prefers-color-scheme: dark) {{ :root:where(:not([data-theme="light"])) .barra {{
  background: var(--cd); }} }}
:root[data-theme="dark"] .barra {{ background: var(--cd); }}
.valor {{ font-variant-numeric: tabular-nums; }}
.nota, .etiqueta {{ color: var(--texto2); }}
.etiqueta {{ color: var(--texto); }}
</style></head><body><main>
<h1>Torneo {d['gauntlet']}</h1>
<p class="sub">{d['partidas_ok']} partidas &middot; {d['semillas']} bloques &middot;
commit <code>{d['commit']}</code> &middot; {t.get('nucleos','?')} nucleos,
governor {t.get('gobernador','?')}</p>

<h2>Puesto medio (menor es mejor)</h2>
{''.join(barras)}

<h2>Latencia y timeouts</h2>
<table><thead><tr><th>snake</th><th>p50 ms</th><th>p99 ms</th><th>max ms</th>
<th>timeouts</th><th>movimientos</th></tr></thead><tbody>{filas_lat}</tbody></table>

<h2>Lo que este reporte no dice</h2>
<p class="sub">Todo lo de arriba es descriptivo: sin p-valores y sin veredicto. Las tres
rivales salen del mismo binario, asi que sus errores estan correlacionados y el campo mide
menos de lo que parece. No hay reproducibilidad bit a bit: se persisten la semilla y el
JSONL de cada partida.</p>
<p class="sub">El documento es <code>reporte.md</code>; esto solo es para mirarlo.</p>
</main></body></html>
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True)
    ap.add_argument("--causas", default=str(RAIZ / "build/release/bin/causas"))
    args = ap.parse_args()
    salida = Path(args.out)
    if not (salida / "torneo.sqlite").exists():
        print(f"ERROR no hay torneo.sqlite en {salida}", file=sys.stderr)
        return 2
    d = recoge(salida, args.causas)
    if d["partidas_ok"] == 0:
        print("ERROR el torneo no tiene ninguna partida buena: no hay reporte que escribir",
              file=sys.stderr)
        return 1
    (salida / "reporte.md").write_text(md(d), encoding="utf-8")
    (salida / "reporte.html").write_text(html(d), encoding="utf-8")
    print(f"escritos {salida}/reporte.md y {salida}/reporte.html")
    print(f"  {d['partidas_ok']} partidas, {d['semillas']} bloques")
    if d["causas"] is None:
        print("  AVISO sin binario de causas: el reporte va sin la tabla de causas de muerte")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
