#!/usr/bin/env python3
"""Tests del orquestador. Se corren solos:  python3 training-room/test_tr.py

No necesitan docker ni arbitro: lo que se prueba es la derivacion, que es donde estan los
errores que mienten en silencio. Que un contenedor no arranque se ve; que los puestos
esten mal repartidos, no.
"""
import importlib.util
from collections import Counter
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

# ---------------------------------------------------------------- persistencia
# Lo que faltaba: los tests probaban la derivacion y nunca la escritura. La primera
# corrida real reventó en el primer INSERT -asiento era NOT NULL y solo se lo ponia a
# nuestra snake- despues de levantar cuatro contenedores y jugar una partida. Un fallo
# que cuesta minutos de docker tiene que salir aqui, en milisegundos y sin docker.
def log_arbitro_falso(ids_y_urls):
    ruta = Path(tempfile.mkstemp(suffix=".log")[1])
    ruta.write_text("\n".join(f"Snake ID: {sid} URL: {url}, Name: {sid}"
                              for sid, url in ids_y_urls) + "\n", encoding="utf-8")
    return ruta


urls = {"v0-baseline": "http://127.0.0.1:9700", "a": "http://127.0.0.1:9701",
        "b": "http://127.0.0.1:9702", "c": "http://127.0.0.1:9703"}
ids = {"v0-baseline": "id0", "a": "ida", "b": "idb", "c": "idc"}
jsonl = jsonl_falso({v: 9 - i for i, v in enumerate(ids.values())}, ganador="id0")
reflog = log_arbitro_falso([(ids[s], u) for s, u in urls.items()])

db = tr.abre_db(":memory:")
orden = ["b", "c", "v0-baseline", "a"]          # asiento 2 para la nuestra
plan = {"id": "g00000", "semilla": 7, "asiento": 2, "comp": ["a", "b", "c"]}
gauntlet = {"nombre": "test", "imagenes": {"zoo/x:1": "sha256:0"}}
tr.guarda(db, gauntlet, plan, jsonl, reflog, 0, urls, orden,
          {"nucleos": 8}, "abc123", "hash", "battlesnake/ours:abc123", gauntlet["imagenes"])

filas = db.execute("SELECT slug, asiento, puesto FROM participantes ORDER BY asiento").fetchall()
comprueba(len(filas) == 4, "se persisten las cuatro snakes, no solo la nuestra")
comprueba([f[0] for f in filas] == orden, "el asiento guardado es la posicion real en el arbitro")
comprueba(all(f[1] is not None for f in filas), "ninguna snake se guarda sin asiento")
comprueba(db.execute("SELECT COUNT(*) FROM latencias").fetchone()[0] == 4,
          "hay una fila de latencias por snake")
fila = db.execute("SELECT semilla, asiento_nuestro, arbitro_rc FROM partidas").fetchone()
comprueba(fila == (7, 2, 0), "la partida guarda semilla, asiento nuestro y codigo del arbitro")

# ---------------------------------------------------------------- reanudacion
# Una corrida de horas tiene que sobrevivir a que la maten. Se comprueba que lo escrito
# persiste y que una segunda pasada reconoce lo ya jugado en vez de repetirlo o borrarlo.
ruta_db = Path(tempfile.mkdtemp()) / "torneo.sqlite"
db1 = tr.abre_db(ruta_db)
tr.guarda(db1, gauntlet, plan, jsonl, reflog, 0, urls, orden,
          {"nucleos": 8}, "abc123", "hash", "battlesnake/ours:abc123", gauntlet["imagenes"])
db1.commit()
db1.close()

db2 = tr.abre_db(ruta_db)
comprueba(db2.execute("SELECT COUNT(*) FROM partidas").fetchone()[0] == 1,
          "lo guardado sigue ahi al reabrir la base: el commit no espera al final")
ya = {f[0] for f in db2.execute("SELECT id FROM partidas WHERE arbitro_rc = 0")}
comprueba(ya == {"g00000"}, "una segunda pasada reconoce la partida ya jugada")

plan_malo = dict(plan, id="g00001")
tr.guarda(db2, gauntlet, plan_malo, jsonl, reflog, 1, urls, orden,
          {"nucleos": 8}, "abc123", "hash", "battlesnake/ours:abc123", gauntlet["imagenes"])
db2.commit()
ya = {f[0] for f in db2.execute("SELECT id FROM partidas WHERE arbitro_rc = 0")}
comprueba("g00001" not in ya, "una partida con el arbitro en error NO cuenta como jugada")

# ---------------------------------------------------------------- cerrojo
# Dos torneos a la vez comparten nombres de contenedor y puertos: el segundo tiene que
# rebotar, no arrancar y destrozar al primero.
import os as _os
primero = tr.toma_el_cerrojo()
hijo = _os.fork()
if hijo == 0:                       # el hijo hereda el fichero pero no el cerrojo
    try:
        tr.toma_el_cerrojo()
        _os._exit(0)                # lo consiguio: el cerrojo no sirve
    except SystemExit:
        _os._exit(3)                # rebotado, que es lo correcto
    except Exception:
        _os._exit(4)
_, estado = _os.waitpid(hijo, 0)
comprueba(_os.WEXITSTATUS(estado) == 3, "un segundo torneo en la misma maquina rebota")
primero.close()
comprueba(tr.toma_el_cerrojo() is not None, "el cerrojo se libera al cerrarse el proceso")

# ---------------------------------------------------------------- incidencias
# El conteo de timeouts sale del stderr del arbitro, no del JSONL, y tiene una exclusion
# que no es cosmetica: `/end` no cuenta. Eremetic Eric fallo las 198 veces al responder
# /end en el torneo real, y contarlo habria dado 198 "incidencias" de algo que no cuesta
# ni un movimiento.
ruta_log = Path(tempfile.mkstemp(suffix=".log")[1])
# Forma REAL del log del arbitro: el fallo ocupa dos lineas y las DOS llevan la url, que
# es lo que hacia contar cada timeout dos veces.
ruta_log.write_text("""INFO Snake ID: id0 URL: http://127.0.0.1:9700, Name: "v0-baseline"
WARN 03:00:33.697467 Request to http://127.0.0.1:9700/move failed
\tError: Post "http://127.0.0.1:9700/move": context deadline exceeded
WARN 03:00:34.121253 Request to http://127.0.0.1:9700/end failed
\tError: Post "http://127.0.0.1:9700/end": context deadline exceeded
WARN 03:00:35.373514 Request to http://127.0.0.1:9700/move failed
\tError: Post "http://127.0.0.1:9700/move": read: connection reset by peer
WARN 03:16:14.274497 Got non-ok status code from http://127.0.0.1:9701/otra/move
""", encoding="utf-8")
inc = tr.incidencias_de(ruta_log, "http://127.0.0.1:9700")
comprueba(inc["timeout"] == 1, "un timeout de /move cuenta UNA vez, no dos (el fallo ocupa dos lineas)")
comprueba(inc["conexion"] == 1, "un fallo de conexion no se cuenta como timeout")
comprueba(inc["status"] == 0, "una queja sobre otra snake no se nos apunta")
inc_otra = tr.incidencias_de(ruta_log, "http://127.0.0.1:9701/otra")
comprueba(inc_otra["status"] == 1, "la queja se apunta a quien le toca")

# ---------------------------------------------------------------- reanaliza
# El test que faltaba. `reanaliza` dijo "recalculadas 800 filas" sin tocar ninguna: los
# grupos de la expresion regular estaban cruzados y el WHERE no casaba con nada. Aqui se
# monta una corrida en miniatura con un log del arbitro con su forma REAL y se exige que
# la fila quede con el valor, no que el script diga que la toco.
import types

dir_run = Path(tempfile.mkdtemp())
(dir_run / "g00000.ref.log").write_text(
    'INFO 03:02:08.578366 Snake ID: id0 URL: http://127.0.0.1:9700, Name: "v0-baseline"\n'
    'INFO 03:02:08.579560 Snake ID: ida URL: http://127.0.0.1:9701/a, Name: "a"\n'
    'WARN Request to http://127.0.0.1:9700/move failed\n'
    'ERROR context deadline exceeded\n'
    'WARN Request to http://127.0.0.1:9700/end failed\n'
    'ERROR context deadline exceeded\n', encoding="utf-8")
db3 = tr.abre_db(dir_run / "torneo.sqlite")
db3.execute("INSERT INTO partidas VALUES ('g00000','t',1,'x.jsonl',0,10,0,'ahora','{}','x')")
for slug in ("v0-baseline", "a"):
    db3.execute("INSERT INTO latencias VALUES ('g00000',?,0,0,0,0,NULL,10)", (slug,))
db3.commit()
db3.close()

rc = tr.cmd_reanaliza(types.SimpleNamespace(out=str(dir_run)))
db3 = tr.abre_db(dir_run / "torneo.sqlite")
valores = dict(db3.execute("SELECT slug, timeouts FROM latencias").fetchall())
comprueba(rc == 0, "reanaliza sale con 0 cuando modifica filas")
comprueba(valores.get("v0-baseline") == 1, "el timeout de /move llega a la fila correcta")
comprueba(valores.get("a") == 0, "una snake sin quejas queda en 0, no en NULL")

# m5 del arnes de mutantes: `modificadas += 1` en vez de `+= cur.rowcount` sobrevivia,
# porque en los casos de arriba las filas iteradas coinciden con las modificadas. Hace
# falta un log que nombre una snake SIN fila en latencias: ahi rowcount es 0 y el contador
# mentiroso suma igual. Es el bug que se publico de verdad: "recalculadas 800 filas" con
# la base intacta.
dir_run2 = Path(tempfile.mkdtemp())
(dir_run2 / "g00000.ref.log").write_text(
    'INFO Snake ID: id0 URL: http://127.0.0.1:9700, Name: "v0-baseline"\n'
    'INFO Snake ID: idz URL: http://127.0.0.1:9709, Name: "fantasma"\n'
    'WARN Request to http://127.0.0.1:9700/move failed\n'
    'ERROR context deadline exceeded\n', encoding="utf-8")
db4 = tr.abre_db(dir_run2 / "torneo.sqlite")
db4.execute("INSERT INTO partidas VALUES ('g00000','t',1,'x.jsonl',0,10,0,'ahora','{}','x')")
db4.execute("INSERT INTO latencias VALUES ('g00000','v0-baseline',0,0,0,0,NULL,10)")
db4.commit(); db4.close()
import io, contextlib
salida_txt = io.StringIO()
with contextlib.redirect_stdout(salida_txt):
    tr.cmd_reanaliza(types.SimpleNamespace(out=str(dir_run2)))
comprueba("modificadas: 1" in salida_txt.getvalue(),
          "el contador cuenta filas modificadas, no snakes vistas en el log")

# m10: sin la guarda, un JSONL sin ninguna serpiente devolvia un resultado vacio en vez
# de None, y el llamante lo tomaba por una partida legible.
sin_snakes = Path(tempfile.mkstemp(suffix=".jsonl")[1])
sin_snakes.write_text(
    '{"id":"x"}\n'
    '{"turn":0,"board":{"snakes":[],"food":[],"hazards":[],"width":11,"height":11}}\n'
    '{"isDraw":false,"winnerId":null}\n', encoding="utf-8")
comprueba(tr.lee_partida(sin_snakes, "/dev/null") is None,
          "un JSONL sin ninguna serpiente se rechaza, no se resume a cero")

vacio = Path(tempfile.mkdtemp())
tr.abre_db(vacio / "torneo.sqlite").close()
comprueba(tr.cmd_reanaliza(types.SimpleNamespace(out=str(vacio))) == 1,
          "un recalculo que no modifica nada FALLA en vez de decir que hizo algo")

# ---------------------------------------------------------------- reporte
spec_r = importlib.util.spec_from_file_location("reporte", RAIZ / "training-room/reporte.py")
rep = importlib.util.module_from_spec(spec_r)
spec_r.loader.exec_module(rep)

dir_rep = Path(tempfile.mkdtemp())
dbr = tr.abre_db(dir_rep / "torneo.sqlite")
dbr.execute("INSERT INTO partidas VALUES ('g0','gauntlet-x',1,'g0.jsonl',0,100,0,'ahora',"
            "'{\"nucleos\": 8, \"gobernador\": \"desconocido\"}','x')")
for i, (slug, puesto) in enumerate([("v0-baseline", 1.0), ("a", 2.5), ("b", 2.5), ("c", 4.0)]):
    dbr.execute("INSERT INTO participantes VALUES ('g0',?,?,'v0','abc','hash','img',?,?,100,NULL)",
                (slug, slug, i, puesto))
    dbr.execute("INSERT INTO latencias VALUES ('g0',?,1,2,3,4,0,100)", (slug,))
dbr.commit(); dbr.close()

datos = rep.recoge(dir_rep, None)
comprueba(datos["partidas_ok"] == 1 and datos["causas"] is None,
          "el reporte se genera sin binario de causas, y lo dice")
texto = rep.md(datos)
comprueba("## T-02" in texto and "v0-baseline" in texto, "el markdown lleva la clasificacion")
comprueba("p-valores" in texto, "el reporte declara que no da veredicto ni p-valores")
comprueba("2.500" in texto, "el puesto compartido llega al reporte sin redondearse a entero")
pagina = rep.html(datos)
comprueba(pagina.count("prefers-color-scheme") >= 2 and 'data-theme="dark"' in pagina,
          "el HTML trae modo oscuro por las dos vias, no solo la del sistema")

vacio2 = Path(tempfile.mkdtemp())
tr.abre_db(vacio2 / "torneo.sqlite").close()
# La tabla de causas tiene que dar cuenta de TODAS las filas. La primera version
# construia las columnas solo con las causas "de verdad" mas ambigua y sobrevivio, y una
# fila con `modelo_discrepa` o `final_no_exportado` desaparecia: las nuestras sumaban 199
# de 200 partidas y la tabla no lo decia.
datos_c = dict(datos)
datos_c["partidas_ok"] = 10
datos_c["causas"] = {
    "v0-baseline": Counter({"cabezazo": 4, "hazard": 3, "modelo_discrepa": 1,
                            "final_no_exportado": 1, "sobrevivio": 1}),
    "rival": Counter({"ambigua": 9, "sobrevivio": 1}),
}
texto_c = rep.md(datos_c)
comprueba("modelo_discrepa" in texto_c and "final_no_exportado" in texto_c,
          "ninguna categoria se cae de la tabla de causas")
comprueba("| 10 |" in texto_c, "cada fila publica su total")
comprueba("AVISO" not in texto_c, "sin descuadre no se avisa de nada")

datos_d = dict(datos_c)
datos_d["causas"] = {"v0-baseline": Counter({"cabezazo": 3, "sobrevivio": 1})}
comprueba("AVISO" in rep.md(datos_d),
          "una fila que no suma las partidas jugadas se denuncia en el propio reporte")
comprueba("De nuestras 9 muertes, 7 estan determinadas" in texto_c,
          "modelo_discrepa y final_no_exportado son muertes, pero NO determinadas")

d_vacio = rep.recoge(vacio2, None)
comprueba(d_vacio["partidas_ok"] == 0, "un torneo sin partidas buenas se detecta antes de escribir")

print()
if fallos:
    print(f"{len(fallos)} fallos")
    sys.exit(1)
print("todos los tests del orquestador pasan")
