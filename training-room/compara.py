#!/usr/bin/env python3
"""Compara dos corridas ya jugadas por BLOQUES PAREADOS, y da un veredicto.

    python3 training-room/compara.py --a docs/results/torneo-v1 \\
                                     --b docs/results/torneo-cuellos-serie

Que es un bloque y por que no es una partida: el prompt maestro fija la unidad de
analisis en **una semilla con la rotacion de asientos completa** (§10.4), no en la
partida. En un juego de cuatro, las cuatro partidas de una misma semilla comparten
comida, hazards y rivales; tratarlas como cuatro observaciones independientes seria
contarse cuatro veces el mismo tablero.

La metrica primaria es **una sola**: la diferencia pareada de puesto medio por bloque.
Todo lo demas -turnos vividos, causas de muerte, latencias- se imprime como descriptivo y
**sin p-valor ni veredicto**: mirar diez metricas a alfa 0.05 da un 40% de probabilidad de
cantar al menos un falso positivo.

Puesto mas BAJO es mejor (1 es ganar), asi que una diferencia negativa significa que B
mejora sobre A.

Lo que este programa se niega a hacer:

  - comparar corridas con gauntlets distintos: en un juego de cuatro la fuerza no es un
    orden total, y un veredicto solo vale contra el campo declarado;
  - comparar corridas con topologias distintas: --paralelo cambia cuantas peticiones
    pierden los rivales, y eso mueve el puesto mas que cualquier heuristica;
  - comparar corridas de la MISMA estrategia creyendo que son dos: se comprueban los
    hashes de config.
"""
import argparse
import json
import math
import pathlib
import sqlite3
import sys

CAMPOS_QUE_INVALIDAN = ("paralelo", "nucleos", "hilos_por_nucleo")


def muere(msg):
    print(f"ERROR {msg}", file=sys.stderr)
    raise SystemExit(2)


def abre(ruta):
    p = pathlib.Path(ruta)
    db = p / "torneo.sqlite" if p.is_dir() else p
    if not db.exists():
        muere(f"no existe {db}")
    return sqlite3.connect(f"file:{db}?mode=ro", uri=True), db


def topologia(c):
    f = c.execute("SELECT topologia FROM partidas WHERE arbitro_rc = 0 LIMIT 1").fetchone()
    if not f:
        return None
    t = json.loads(f[0])
    t.setdefault("paralelo", 1)   # las corridas anteriores a la funcion son en serie
    return t


def nuestro_slug(c):
    """La snake nuestra es la que no sale del zoo: su `imagen` no empieza por `zoo/`."""
    filas = c.execute(
        "SELECT slug, COUNT(*) FROM participantes WHERE imagen NOT LIKE 'zoo/%' "
        "GROUP BY slug ORDER BY 2 DESC").fetchall()
    if not filas:
        muere("no encuentro ninguna snake nuestra en la corrida")
    if len(filas) > 1:
        muere(f"hay mas de una snake nuestra en la misma corrida: {[f[0] for f in filas]}")
    return filas[0][0]


def bloques(c, slug):
    """{semilla: {asiento: puesto}} y los descriptivos.

    El asiento se guarda, no se descarta: un bloque solo vale si las dos corridas lo
    jugaron desde los MISMOS asientos. Una corrida cortada a mitad deja un ultimo bloque
    con un asiento en vez de cuatro, y promediar ese uno contra los cuatro del otro lado
    mete justo el sesgo de asiento que la rotacion existe para cancelar.
    """
    filas = c.execute(
        "SELECT p.semilla, p.asiento_nuestro, pa.puesto, pa.turnos_sobrevividos, "
        "pa.causa_muerte FROM partidas p JOIN participantes pa ON pa.partida_id = p.id "
        "WHERE p.arbitro_rc = 0 AND pa.slug = ? AND pa.puesto IS NOT NULL",
        (slug,)).fetchall()
    por_semilla, turnos, causas = {}, [], {}
    for semilla, asiento, puesto, sobrevividos, causa in filas:
        por_semilla.setdefault(semilla, {})[asiento] = puesto
        if sobrevividos is not None:
            turnos.append(sobrevividos)
        causas[causa or "sin_dato"] = causas.get(causa or "sin_dato", 0) + 1
    return por_semilla, turnos, causas


def salud_del_campo(c):
    """Cuantas peticiones pierden los RIVALES por partida, y de que forma.

    Por que va al lado del veredicto: un rival que no contesta -llegue tarde o cierre la
    conexion- recibe el movimiento por defecto del arbitro, y ese movimiento suele
    matarlo. Un campo averiado regala puestos. Si las dos corridas no tuvieron el campo
    igual de sano, parte de la diferencia no es la estrategia.

    Medido: en torneo-v1 los rivales perdian 1.76 peticiones por partida y en
    torneo-cuellos-serie 0.90. La linea base de v0 se midio contra un campo el doble de
    averiado que el de ahora, asi que favorece a v0.
    """
    # NULL no es cero. Una corrida anterior a estas columnas tiene NULL, y leerlo como 0
    # diria que su campo estaba sano cuando lo que pasa es que no se midio. Con el aviso
    # de mas abajo, ese cero falso invertiria la conclusion.
    # La columna puede no existir siquiera: esto abre en solo lectura a proposito -una
    # herramienta de analisis no toca los datos- asi que no puede migrar la tabla.
    hay = {f[1] for f in c.execute("PRAGMA table_info(latencias)")}
    if "fallos_conexion" not in hay:
        return {"medido": False, "filas_sin_medir": 0, "motivo": "la columna no existe",
                "timeouts": None, "conexion": None, "otros": None,
                "fallos_por_partida": None}
    sin_medir = c.execute(
        "SELECT COUNT(*) FROM latencias WHERE fallos_conexion IS NULL").fetchone()[0]
    if sin_medir:
        return {"medido": False, "filas_sin_medir": sin_medir,
                "motivo": f"{sin_medir} filas sin rellenar",
                "timeouts": None, "conexion": None, "otros": None,
                "fallos_por_partida": None}
    fila = c.execute(
        "SELECT SUM(l.timeouts), SUM(COALESCE(l.fallos_conexion,0)), "
        "       SUM(COALESCE(l.fallos_status,0) + COALESCE(l.fallos_json,0) "
        "           + COALESCE(l.fallos_movimiento,0)), SUM(l.movimientos) "
        "FROM latencias l JOIN partidas g ON g.id = l.partida_id "
        "JOIN participantes pa ON pa.partida_id = l.partida_id AND pa.slug = l.slug "
        "WHERE g.arbitro_rc = 0 AND pa.imagen LIKE 'zoo/%'").fetchone()
    n = c.execute("SELECT COUNT(*) FROM partidas WHERE arbitro_rc = 0").fetchone()[0] or 1
    to, con, otros, movs = [x or 0 for x in fila]
    return {"medido": True, "timeouts": to, "conexion": con, "otros": otros,
            "movimientos_rivales": movs,
            "fallos_por_partida": round((to + con + otros) / n, 3)}


def media(xs):
    return sum(xs) / len(xs) if xs else float("nan")


def desviacion(xs):
    if len(xs) < 2:
        return float("nan")
    m = media(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - 1))


def t_pareada(ds):
    """t de Student sobre las diferencias pareadas. Devuelve (t, gl, ic95)."""
    n = len(ds)
    if n < 2:
        return float("nan"), 0, (float("nan"), float("nan"))
    m, s = media(ds), desviacion(ds)
    if s == 0:
        return (float("inf") if m else 0.0), n - 1, (m, m)
    err = s / math.sqrt(n)
    # 1.96 es la normal; con n pequeño se queda corto, asi que se usa la t al 95%.
    t_critica = {2: 12.71, 3: 4.30, 4: 3.18, 5: 2.78, 6: 2.57, 7: 2.45, 8: 2.36,
                 9: 2.31, 10: 2.26, 11: 2.23, 12: 2.20, 13: 2.18, 14: 2.16, 15: 2.14,
                 16: 2.12, 17: 2.11, 18: 2.10, 19: 2.09, 20: 2.09}.get(n - 1, 1.96)
    return m / err, n - 1, (m - t_critica * err, m + t_critica * err)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", required=True, help="corrida de referencia (la linea base)")
    ap.add_argument("--b", required=True, help="corrida candidata")
    ap.add_argument("--delta", type=float, default=0.10,
                    help="ventaja minima de puesto medio que se considera util (default 0.10)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    ca, da = abre(args.a)
    cb, db_ = abre(args.b)

    ga = ca.execute("SELECT DISTINCT gauntlet FROM partidas").fetchall()
    gb = cb.execute("SELECT DISTINCT gauntlet FROM partidas").fetchall()
    if ga != gb:
        muere(f"gauntlets distintos: {ga} vs {gb}. En un juego de cuatro la fuerza no es "
              "un orden total; un veredicto solo vale contra el campo declarado.")

    ta, tb = topologia(ca), topologia(cb)
    if ta is None or tb is None:
        muere("alguna de las dos corridas no tiene ni una partida buena")
    distintos = [c for c in CAMPOS_QUE_INVALIDAN if ta.get(c) != tb.get(c)]
    if distintos:
        muere("topologias distintas: "
              + "; ".join(f"{c}: {ta.get(c)} vs {tb.get(c)}" for c in distintos)
              + ".\n  --paralelo cambia cuantas peticiones pierden los rivales, y cada "
                "timeout suyo es\n  un movimiento por defecto que suele matarlos: eso "
                "mueve el puesto mas que\n  cualquier heuristica. Estas dos no se comparan.")

    sa, sb = nuestro_slug(ca), nuestro_slug(cb)
    ha = ca.execute("SELECT DISTINCT hash_config FROM participantes WHERE slug = ?",
                    (sa,)).fetchone()
    hb = cb.execute("SELECT DISTINCT hash_config FROM participantes WHERE slug = ?",
                    (sb,)).fetchone()
    if ha and hb and ha[0] == hb[0]:
        muere(f"las dos corridas usan el MISMO config (hash {ha[0]}): no hay nada que "
              "comparar. Un A/A se hace a proposito, con --delta y a sabiendas.")

    salud_a, salud_b = salud_del_campo(ca), salud_del_campo(cb)

    ba, turnos_a, causas_a = bloques(ca, sa)
    bb, turnos_b, causas_b = bloques(cb, sb)
    comunes = sorted(set(ba) & set(bb))
    if not comunes:
        muere("las dos corridas no comparten ninguna semilla: no hay bloques pareados. "
              "Usa el mismo --seed-base.")

    filas, ds, incompletos = [], [], []
    for s in comunes:
        asientos = set(ba[s]) & set(bb[s])
        if set(ba[s]) != set(bb[s]):
            incompletos.append((s, sorted(ba[s]), sorted(bb[s])))
            continue
        ma = media([ba[s][a] for a in sorted(asientos)])
        mb = media([bb[s][a] for a in sorted(asientos)])
        d = mb - ma
        ds.append(d)
        filas.append((s, len(asientos), ma, len(asientos), mb, d))

    if not filas:
        muere("ninguna semilla comun se jugo desde los mismos asientos en las dos "
              "corridas: sin rotacion pareada no hay bloque que comparar.")

    t, gl, (lo, hi) = t_pareada(ds)
    mejora = media(ds)
    # Puesto mas bajo es mejor. El veredicto se decide por el IC95, no por el signo de la
    # media: una media negativa con el IC cruzando el cero es ruido, no una mejora.
    if hi < 0 and abs(mejora) >= args.delta:
        veredicto = "MEJORA"
    elif lo > 0 and abs(mejora) >= args.delta:
        veredicto = "EMPEORA"
    else:
        veredicto = "NO CONCLUYENTE"

    salida = {
        "a": str(da.parent.name), "b": str(db_.parent.name),
        "slug_a": sa, "slug_b": sb,
        "bloques_pareados": len(filas),
        "bloques_descartados_por_incompletos": [
            {"semilla": s, "asientos_a": a, "asientos_b": b} for s, a, b in incompletos],
        "partidas_a": sum(f[1] for f in filas), "partidas_b": sum(f[3] for f in filas),
        "puesto_medio_a": round(media([f[2] for f in filas]), 4),
        "puesto_medio_b": round(media([f[4] for f in filas]), 4),
        "diferencia_pareada": round(mejora, 4),
        "ic95": [round(lo, 4), round(hi, 4)],
        "t": round(t, 3) if math.isfinite(t) else None, "gl": gl,
        "delta_declarado": args.delta,
        "veredicto": veredicto,
        "salud_del_campo": {"a": salud_a, "b": salud_b},
        "descriptivo_sin_veredicto": {
            "turnos_vividos_a": round(media(turnos_a), 1),
            "turnos_vividos_b": round(media(turnos_b), 1),
            "causas_a": causas_a, "causas_b": causas_b,
        },
    }

    if args.json:
        print(json.dumps(salida, indent=2, ensure_ascii=False))
        return 0

    print(f"A = {salida['a']}  ({sa}, {salida['partidas_a']} partidas)")
    print(f"B = {salida['b']}  ({sb}, {salida['partidas_b']} partidas)")
    print(f"{len(filas)} bloques pareados (semilla x rotacion de asientos)")
    for s_, aa, bbb in incompletos:
        print(f"  DESCARTADO bloque {s_}: asientos {aa} en A y {bbb} en B. Un bloque a "
              "medias no cancela el sesgo de asiento.")
    print()
    print("| semilla | n_A | puesto A | n_B | puesto B | dif |")
    print("|---:|---:|---:|---:|---:|---:|")
    for s, na, ma, nb, mb, d in filas:
        print(f"| {s} | {na} | {ma:.3f} | {nb} | {mb:.3f} | {d:+.3f} |")
    print(f"\nMETRICA PRIMARIA — diferencia pareada de puesto medio por bloque")
    print(f"  puesto medio   A {salida['puesto_medio_a']:.3f}   B {salida['puesto_medio_b']:.3f}")
    print(f"  diferencia     {mejora:+.4f}   (negativo = B mejor)")
    print(f"  IC95           [{lo:+.4f}, {hi:+.4f}]   sobre {len(filas)} bloques")
    print(f"  delta util     {args.delta}")
    print(f"\n  VEREDICTO: {veredicto}")
    if veredicto == "NO CONCLUYENTE":
        print("  El intervalo cruza el cero o el efecto no llega al delta declarado. Eso")
        print("  NO dice que B sea igual que A: dice que con estos bloques no se distingue.")
    print(f"\n-- salud del campo (peticiones que los RIVALES no contestaron) --")
    for etiqueta, s_ in (("A", salud_a), ("B", salud_b)):
        if not s_["medido"]:
            print(f"  {etiqueta}  SIN MEDIR ({s_['motivo']}). "
                  "Corre `tr.py reanaliza --out <corrida>`.")
        else:
            print(f"  {etiqueta}  {s_['fallos_por_partida']}/partida   "
                  f"(timeout {s_['timeouts']}, conexion {s_['conexion']}, "
                  f"otros {s_['otros']})")
    if not (salud_a["medido"] and salud_b["medido"]):
        print("  AVISO sin la salud de las dos, el veredicto de arriba va sin su contexto:"
              "\n        un campo averiado regala puestos y no se sabe si lo estaba.")
    fa = salud_a["fallos_por_partida"] or 0.0
    fb = salud_b["fallos_por_partida"] or 0.0
    if salud_a["medido"] and salud_b["medido"] and max(fa, fb) > 0 \
            and min(fa, fb) < 0.6 * max(fa, fb):
        peor, mejor = ("A", "B") if fa > fb else ("B", "A")
        print(f"  AVISO los campos NO estaban igual de sanos: en {peor} los rivales "
              f"fallaron\n        {max(fa, fb) / max(min(fa, fb), 1e-9):.1f}x mas que en "
              f"{mejor}. Un rival que no contesta recibe el movimiento\n        por "
              f"defecto y suele morir, asi que {peor} regalo puestos. Parte de la "
              f"diferencia\n        de arriba no es la estrategia.")
    print("\n-- descriptivo, SIN veredicto ni p-valores --")
    print(f"  turnos vividos   A {salida['descriptivo_sin_veredicto']['turnos_vividos_a']}"
          f"   B {salida['descriptivo_sin_veredicto']['turnos_vividos_b']}")
    print(f"  causas A  {causas_a}")
    print(f"  causas B  {causas_b}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
