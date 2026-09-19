#!/usr/bin/env python3
"""Tests del orquestador. Se corren solos:  python3 training-room/test_tr.py

No necesitan docker ni arbitro: lo que se prueba es la derivacion, que es donde estan los
errores que mienten en silencio. Que un contenedor no arranque se ve; que los puestos
esten mal repartidos, no.
"""
import importlib.util
from collections import Counter
import json
import contextlib
import io
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
          {"nucleos": 8}, "abc123", "hash", "battlesnake/ours:abc123", gauntlet["imagenes"],
          "v0-baseline")

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
          {"nucleos": 8}, "abc123", "hash", "battlesnake/ours:abc123", gauntlet["imagenes"],
          "v0-baseline")
db1.commit()
db1.close()

db2 = tr.abre_db(ruta_db)
comprueba(db2.execute("SELECT COUNT(*) FROM partidas").fetchone()[0] == 1,
          "lo guardado sigue ahi al reabrir la base: el commit no espera al final")
ya = {f[0] for f in db2.execute("SELECT id FROM partidas WHERE arbitro_rc = 0")}
comprueba(ya == {"g00000"}, "una segunda pasada reconoce la partida ya jugada")

plan_malo = dict(plan, id="g00001")
tr.guarda(db2, gauntlet, plan_malo, jsonl, reflog, 1, urls, orden,
          {"nucleos": 8}, "abc123", "hash", "battlesnake/ours:abc123", gauntlet["imagenes"],
          "v0-baseline")
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
    db3.execute("INSERT INTO latencias (partida_id, slug, p50, p95, p99, maximo, timeouts, movimientos) VALUES ('g00000',?,0,0,0,0,NULL,10)",
                (slug,))
db3.commit()
db3.close()

rc = tr.cmd_reanaliza(types.SimpleNamespace(out=str(dir_run)))
db3 = tr.abre_db(dir_run / "torneo.sqlite")
valores = dict(db3.execute("SELECT slug, timeouts FROM latencias").fetchall())
comprueba(rc == 0, "reanaliza sale con 0 cuando modifica filas")
# El nombre con el que jugamos sale del config, no de una constante: un torneo de v1 salia
# etiquetado como v0 en el JSONL y en el reporte.
comprueba(tr.slug_del_config("snake/config/v1.json") == "v1",
          "el config v1 hace que juguemos como 'v1'")
# `default.json` fue v0 hasta que v4 gano su A/B; ahora ES v4, y la etiqueta lo sigue.
# v0 no se pierde: vive en `v0-baseline.json` con nombre propio, como manda §9.
comprueba(tr.slug_del_config("snake/config/default.json") == "v5-longitud",
          "el config por defecto es v5, la ultima version que gano su A/B")
comprueba(tr.slug_del_config("snake/config/v0-baseline.json") == "v0-baseline",
          "y v0 sigue existiendo con su nombre propio, que es la referencia fija")
comprueba((tr.RAIZ / "snake/config/v0-baseline.json").exists(),
          "el fichero de v0 existe de verdad: la referencia fija no se borra")
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
db4.execute("INSERT INTO latencias (partida_id, slug, p50, p95, p99, maximo, timeouts, movimientos) "
            "VALUES ('g00000','v0-baseline',0,0,0,0,NULL,10)")
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
    dbr.execute("INSERT INTO latencias (partida_id, slug, p50, p95, p99, maximo, timeouts, movimientos) VALUES ('g0',?,1,2,3,4,0,100)",
                (slug,))
dbr.commit(); dbr.close()

datos = rep.recoge(dir_rep, None)
comprueba(datos["partidas_ok"] == 1 and datos["causas"] is None,
          "el reporte se genera sin binario de causas, y lo dice")
texto = rep.md(datos)
comprueba("## T-02" in texto and "v0-baseline" in texto, "el markdown lleva la clasificacion")
comprueba("p-valores" in texto, "el reporte declara que no da veredicto ni p-valores")
# Una corrida en paralelo no mide latencia, y eso tiene que estar donde se lee primero.
datos_par = dict(datos)
datos_par["topologia"] = {"nucleos": 8, "paralelo": 2, "latencia_valida": False}
texto_par = rep.md(datos_par)
comprueba("NO mide latencia" in texto_par,
          "una corrida en paralelo declara que no mide latencia, arriba del todo")
comprueba(texto_par.index("NO mide latencia") < texto_par.index("T-02"),
          "esa advertencia va ANTES de la clasificacion, no en una nota al pie")
comprueba("2.500" in texto, "el puesto compartido llega al reporte sin redondearse a entero")
# T-03 agrupa por punto de salida, no por el orden de los argumentos: ese orden no decide
# donde sale nadie (el arbitro recorre un mapa de Go). Y si no hay JSONL, lo dice.
comprueba("punto de salida" in texto and "asiento 0" not in texto,
          "T-03 agrupa por punto de salida, no por asiento")
comprueba("Sin datos" in texto,
          "sin JSONL, T-03 declara que falta la comprobacion en vez de salir vacia")
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

# --- resolucion del --config ------------------------------------------------------
# `deploy/cloud-run.sh --config` toma un nombre y `tr.py --config` tomaba una ruta. Un
# torneo de 60 partidas midiendo la estrategia equivocada cuesta una hora que no hay.
por_nombre = tr.resuelve_config("cuellos")
por_ruta = tr.resuelve_config("snake/config/cuellos.json")
comprueba(por_nombre.resolve() == por_ruta.resolve(),
          "--config acepta el nombre y la ruta, y los dos dan el mismo fichero")
comprueba(tr.hash_config(por_nombre) == tr.hash_config(por_ruta),
          "y por tanto el mismo hash, que es lo que identifica la estrategia en la base")
comprueba(tr.resuelve_config(None).stem == "default",
          "sin --config se juega el default del repo")
try:
    tr.resuelve_config("no-existe-jamas")
    comprueba(False, "un config inexistente aborta")
except SystemExit:
    comprueba(True, "un config inexistente aborta")
faltan = set(tr.configs_disponibles()) - {p.stem for p in
                                          (tr.RAIZ / "snake/config").glob("*.json")}
comprueba(not faltan, "la lista que se le ofrece al usuario sale del disco, no de una constante")

# --- no se reanuda una corrida con otra topologia ----------------------------------
# Las 10 primeras partidas de torneo-cuellos se jugaron con --paralelo 2 y el resto iba a
# jugarse en serie. Mezcladas en el mismo SQLite no se ve en ninguna tabla, y la mitad
# paralela sale con nuestro puesto inflado porque los rivales pierden 7x mas peticiones.
tmp_topo = Path(tempfile.mkdtemp())
db_topo = tr.abre_db(tmp_topo / "torneo.sqlite")
db_topo.execute(
    "INSERT INTO partidas (id, gauntlet, semilla, jsonl, asiento_nuestro, turnos, "
    "arbitro_rc, empezada_en, topologia, rng_version) VALUES "
    "('g00000','g',1,'x',0,10,0,'t',?,'r')",
    (json.dumps({"nucleos": 8, "hilos_por_nucleo": "1", "paralelo": 2}),))
db_topo.commit()

try:
    tr.comprueba_topologia(db_topo, {"nucleos": 8, "hilos_por_nucleo": "1", "paralelo": 1})
    comprueba(False, "reanudar con otro --paralelo aborta")
except SystemExit:
    comprueba(True, "reanudar con otro --paralelo aborta")

try:
    tr.comprueba_topologia(db_topo, {"nucleos": 8, "hilos_por_nucleo": "1", "paralelo": 2})
    comprueba(True, "con la misma topologia se reanuda sin protestar")
except SystemExit:
    comprueba(False, "con la misma topologia se reanuda sin protestar")

try:
    tr.comprueba_topologia(db_topo, {"nucleos": 4, "hilos_por_nucleo": "1", "paralelo": 2})
    comprueba(False, "y cambiar de maquina tambien aborta, no solo el paralelo")
except SystemExit:
    comprueba(True, "y cambiar de maquina tambien aborta, no solo el paralelo")

db_vacia = tr.abre_db(Path(tempfile.mkdtemp()) / "t.sqlite")
try:
    tr.comprueba_topologia(db_vacia, {"nucleos": 8, "paralelo": 1})
    comprueba(True, "una corrida nueva no tiene nada con que chocar")
except SystemExit:
    comprueba(False, "una corrida nueva no tiene nada con que chocar")

# El caso de torneo-v1: las corridas viejas no llevaban el campo `paralelo` porque son
# anteriores a la funcion. Eso es paralelo 1, no "desconocido".
db_vieja = tr.abre_db(Path(tempfile.mkdtemp()) / "t.sqlite")
db_vieja.execute(
    "INSERT INTO partidas (id, gauntlet, semilla, jsonl, asiento_nuestro, turnos, "
    "arbitro_rc, empezada_en, topologia, rng_version) VALUES "
    "('g00000','g',1,'x',0,10,0,'t',?,'r')",
    (json.dumps({"nucleos": 8, "hilos_por_nucleo": "1"}),))
db_vieja.commit()
try:
    tr.comprueba_topologia(db_vieja, {"nucleos": 8, "hilos_por_nucleo": "1", "paralelo": 2})
    comprueba(False, "una corrida sin campo `paralelo` cuenta como serie, y 2 no es 1")
except SystemExit:
    comprueba(True, "una corrida sin campo `paralelo` cuenta como serie, y 2 no es 1")

# --- no se reanuda una corrida con OTRA estrategia --------------------------------
# El §10.4 dice que un A/B por los pelos se amplia con mas bloques del MISMO run. Si entre
# medias cambia el config -aunque sea el tope de profundidad, que parece inocente-, las
# partidas nuevas las juega otra snake y el veredicto promedia dos cosas.
tmp_cfg = Path(tempfile.mkdtemp())
db_cfg = tr.abre_db(tmp_cfg / "torneo.sqlite")
db_cfg.execute(
    "INSERT INTO partidas (id, gauntlet, semilla, jsonl, asiento_nuestro, turnos, "
    "arbitro_rc, empezada_en, topologia, rng_version) VALUES "
    "('g00000','g',1,'x',0,10,0,'t','{}','r')")
db_cfg.execute(
    "INSERT INTO participantes (partida_id, slug, nombre, hash_config, imagen, asiento, "
    "puesto) VALUES ('g00000','v3-busqueda','v3','HASH_VIEJO','local/snake',0,2.0)")
db_cfg.execute(
    "INSERT INTO participantes (partida_id, slug, nombre, hash_config, imagen, asiento, "
    "puesto) VALUES ('g00000','rival','rival','h2','zoo/x',1,1.0)")
db_cfg.commit()

try:
    tr.comprueba_config(db_cfg, "HASH_NUEVO", "v3-busqueda")
    comprueba(False, "reanudar con otro hash de config aborta")
except SystemExit:
    comprueba(True, "reanudar con otro hash de config aborta")

try:
    tr.comprueba_config(db_cfg, "HASH_VIEJO", "otra-snake")
    comprueba(False, "y cambiar de slug tambien aborta, aunque el hash coincidiera")
except SystemExit:
    comprueba(True, "y cambiar de slug tambien aborta, aunque el hash coincidiera")

try:
    tr.comprueba_config(db_cfg, "HASH_VIEJO", "v3-busqueda")
    comprueba(True, "con la misma estrategia se amplia sin protestar")
except SystemExit:
    comprueba(False, "con la misma estrategia se amplia sin protestar")

try:
    tr.comprueba_config(tr.abre_db(Path(tempfile.mkdtemp()) / "t.sqlite"), "h", "s")
    comprueba(True, "una corrida nueva no tiene con que chocar")
except SystemExit:
    comprueba(False, "una corrida nueva no tiene con que chocar")

# --- la basura de la corrida anterior se limpia sola --------------------------------
# Una corrida en serie deja `tr-ours` en el 9700; la siguiente con --paralelo 2 quiere
# `tr-ours-w0` en el MISMO 9700 y chocaba contra un contenedor de hace tres horas.
_vistos = []


class _Ps:
    returncode = 0
    stdout = "abc123 def456\n"
    stderr = ""


class _Inspect:
    returncode = 0
    stdout = "/tr-ours\n/zoo-eremetic-eric\n"
    stderr = ""


def _falso_corre(cmd, **kw):
    _vistos.append(cmd)
    if "ps" in cmd:
        return _Ps()
    if "inspect" in cmd:
        return _Inspect()
    return _Ps()


_corre_real = tr.corre
tr.corre = _falso_corre
try:
    borrados = tr.libera_nuestros_contenedores("docker")
finally:
    tr.corre = _corre_real

comprueba(borrados == ["abc123", "def456"], "borra lo que docker ps le devuelve")
filtros = [c for c in _vistos if "ps" in c][0]
comprueba("name=^tr-ours" in filtros and "name=^zoo-" in filtros,
          "solo mira nuestro espacio de nombres, anclado al principio")
comprueba(all("testing" not in " ".join(c) for c in _vistos),
          "no toca ningun contenedor ajeno al Training Room")
rm = [c for c in _vistos if "rm" in c][0]
comprueba(rm[-2:] == ["abc123", "def456"], "y los borra por id, no por nombre adivinado")

# --- un arranque fallido tiene que decir POR QUE ----------------------------------
# "ERROR no arranco el contenedor de nuestra snake" se lee igual con el puerto ocupado,
# con un cpuset invalido y con la imagen rota. El de los rivales ya traia el stderr de
# docker; el nuestro no, y costo un turno entero adivinando.
class _Falla:
    returncode = 1
    stderr = "docker: Error response from daemon: port is already allocated."
    stdout = ""


_corre_real = tr.corre
tr.corre = lambda cmd, **kw: _Falla() if "run" in cmd else _corre_real(["true"])
try:
    tr.arranca_la_nuestra("docker", "img", 9800, 1, "0", "512m", False)
    comprueba(False, "un arranque fallido aborta")
except SystemExit:
    comprueba(True, "un arranque fallido aborta")
finally:
    tr.corre = _corre_real

_salida = io.StringIO()
tr.corre = lambda cmd, **kw: _Falla() if "run" in cmd else _corre_real(["true"])
try:
    with contextlib.redirect_stderr(_salida):
        tr.arranca_la_nuestra("docker", "img", 9800, 1, "0", "512m", False)
except SystemExit:
    pass
finally:
    tr.corre = _corre_real
comprueba("port is already allocated" in _salida.getvalue(),
          "y el mensaje lleva lo que dijo docker, no solo que fallo")

d_vacio = rep.recoge(vacio2, None)
comprueba(d_vacio["partidas_ok"] == 0, "un torneo sin partidas buenas se detecta antes de escribir")


# ==================================================================================
# compara.py — la comparacion pareada por bloques
# ==================================================================================
cmp_spec = importlib.util.spec_from_file_location("compara", RAIZ / "training-room/compara.py")
cmpm = importlib.util.module_from_spec(cmp_spec)
cmp_spec.loader.exec_module(cmpm)


def _corrida(dirname, slug, puestos_por_semilla, topo, hash_cfg, gauntlet="gauntlet-v1",
             fallos_rival=0):
    """Fabrica una corrida: {semilla: [puesto de cada asiento]}."""
    d = Path(tempfile.mkdtemp()) / dirname
    d.mkdir()
    db = tr.abre_db(d / "torneo.sqlite")
    g = 0
    for semilla, puestos in sorted(puestos_por_semilla.items()):
        for asiento, puesto in enumerate(puestos):
            pid = f"g{g:05d}"
            g += 1
            db.execute("INSERT INTO partidas VALUES (?,?,?,?,?,?,?,?,?,?)",
                       (pid, gauntlet, semilla, "x", asiento, 100, 0, "t",
                        json.dumps(topo), "r"))
            db.execute("INSERT INTO participantes VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                       (pid, slug, slug, "v", "c", hash_cfg, "local/snake", asiento,
                        puesto, 100, "cabezazo"))
            db.execute("INSERT INTO participantes VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                       (pid, "rival", "rival", "v", "c", "h2", "zoo/battlesnake-rs",
                        asiento + 1, 1.0, 120, "sobrevivio"))
            db.execute("INSERT INTO latencias (partida_id, slug, timeouts, movimientos, "
                       "fallos_conexion) VALUES (?,?,?,?,?)",
                       (pid, "rival", 0, 100, fallos_rival))
    db.commit()
    db.close()
    return d


SERIE = {"nucleos": 8, "hilos_por_nucleo": "1", "paralelo": 1}
PAR2 = {"nucleos": 8, "hilos_por_nucleo": "1", "paralelo": 2}


def _compara_texto(a, b, extra=()):
    import subprocess
    return subprocess.run([sys.executable, str(RAIZ / "training-room/compara.py"),
                           "--a", str(a), "--b", str(b), *extra],
                          capture_output=True, text=True).stdout


def _compara(a, b, extra=()):
    import subprocess
    r = subprocess.run([sys.executable, str(RAIZ / "training-room/compara.py"),
                        "--a", str(a), "--b", str(b), "--json", *extra],
                       capture_output=True, text=True)
    return r


# B gana un puesto entero en cada bloque: eso tiene que salir MEJORA.
a1 = _corrida("a", "v0-baseline", {1: [3, 3, 3, 3], 2: [3, 3, 3, 3], 3: [3, 3, 3, 3]},
              SERIE, "hA")
b1 = _corrida("b", "cuellos", {1: [2, 2, 2, 2], 2: [2, 2, 2, 2], 3: [2, 2, 2, 2]},
              SERIE, "hB")
r = _compara(a1, b1)
d = json.loads(r.stdout)
comprueba(d["veredicto"] == "MEJORA", "una mejora de un puesto entero se detecta")
comprueba(d["diferencia_pareada"] == -1.0, "y el signo es el correcto: negativo es mejor")
comprueba(d["bloques_pareados"] == 3, "la unidad es el bloque, no la partida")
comprueba(d["partidas_b"] == 12, "aunque las partidas se cuenten aparte")

# Ruido simetrico: no puede salir veredicto.
a2 = _corrida("a", "v0-baseline", {1: [2, 3, 2, 3], 2: [3, 2, 3, 2], 3: [2, 3, 3, 2]},
              SERIE, "hA")
b2 = _corrida("b", "cuellos", {1: [3, 2, 3, 2], 2: [2, 3, 2, 3], 3: [3, 2, 2, 3]},
              SERIE, "hB")
comprueba(json.loads(_compara(a2, b2).stdout)["veredicto"] == "NO CONCLUYENTE",
          "el ruido simetrico no produce veredicto")

# Una mejora real pero por debajo del delta declarado tampoco entra.
a3 = _corrida("a", "v0-baseline", {1: [3, 3, 3, 3], 2: [3, 3, 3, 3], 3: [3, 3, 3, 3]},
              SERIE, "hA")
b3 = _corrida("b", "cuellos", {1: [2.98] * 4, 2: [2.98] * 4, 3: [2.98] * 4}, SERIE, "hB")
comprueba(json.loads(_compara(a3, b3).stdout)["veredicto"] == "NO CONCLUYENTE",
          "una mejora por debajo del delta declarado no entra aunque sea consistente")
comprueba(json.loads(_compara(a3, b3, ("--delta", "0.01")).stdout)["veredicto"] == "MEJORA",
          "y con el delta bajado a sabiendas, si")

# Los cuatro casos en que se tiene que NEGAR.
comprueba(_compara(a1, _corrida("b", "cuellos", {1: [2] * 4}, PAR2, "hB")).returncode == 2,
          "se niega a comparar topologias distintas")
comprueba(_compara(a1, _corrida("b", "cuellos", {1: [2] * 4}, SERIE, "hB",
                                gauntlet="gauntlet-v2")).returncode == 2,
          "se niega a comparar gauntlets distintos")
comprueba(_compara(a1, _corrida("b", "v0-baseline", {1: [2] * 4}, SERIE, "hA")).returncode == 2,
          "se niega a comparar una corrida consigo misma (mismo hash de config)")
comprueba(_compara(a1, _corrida("b", "cuellos", {99: [2] * 4}, SERIE, "hB")).returncode == 2,
          "se niega si no comparten ni una semilla: sin bloques no hay pareo")

# La salud del campo va al lado del veredicto. Un rival que no contesta -llegue tarde o
# cierre la conexion- recibe el movimiento por defecto y suele morir: un campo averiado
# regala puestos. Medido de verdad: torneo-v1 tuvo 1.76 fallos de rival por partida y
# torneo-cuellos-serie 0.90, o sea que la linea base de v0 jugo contra un campo el doble
# de roto.
a6 = _corrida("a", "v0-baseline", {1: [3] * 4, 2: [3] * 4}, SERIE, "hA", fallos_rival=4)
b6 = _corrida("b", "cuellos", {1: [3] * 4, 2: [3] * 4}, SERIE, "hB", fallos_rival=0)
r6 = _compara(a6, b6)
d6 = json.loads(r6.stdout)
comprueba(d6["salud_del_campo"]["a"]["fallos_por_partida"] == 4.0
          and d6["salud_del_campo"]["b"]["fallos_por_partida"] == 0.0,
          "el veredicto viene con la salud del campo de cada corrida")

r6t = _compara_texto(a6, b6)
comprueba("AVISO los campos NO estaban igual de sanos" in r6t,
          "y avisa cuando los dos campos no eran comparables")

a7 = _corrida("a", "v0-baseline", {1: [3] * 4}, SERIE, "hA", fallos_rival=2)
b7 = _corrida("b", "cuellos", {1: [3] * 4}, SERIE, "hB", fallos_rival=2)
comprueba("AVISO los campos NO" not in _compara_texto(a7, b7),
          "con los dos campos igual de sanos no avisa de nada")

# Un bloque a medias NO es un bloque. La corrida de v1 se corto en 61 partidas y dejo un
# ultimo bloque con un asiento en vez de cuatro; promediar ese uno contra los cuatro del
# otro lado mete el sesgo de asiento que la rotacion existe para cancelar.
a5 = _corrida("a", "v0-baseline", {1: [3, 3, 3, 3], 2: [3, 3, 3, 3], 3: [3, 3, 3, 3]},
              SERIE, "hA")
b5 = _corrida("b", "cuellos", {1: [2, 2, 2, 2], 2: [2, 2, 2, 2], 3: [2]}, SERIE, "hB")
d5 = json.loads(_compara(a5, b5).stdout)
comprueba(d5["bloques_pareados"] == 2, "un bloque con menos asientos se descarta")
comprueba(len(d5["bloques_descartados_por_incompletos"]) == 1,
          "y se dice cual y con que asientos, no se descarta en silencio")
comprueba(d5["bloques_descartados_por_incompletos"][0]["semilla"] == 3,
          "el bloque descartado es el que estaba a medias")

solo_medios = _corrida("b", "cuellos", {1: [2], 2: [2], 3: [2]}, SERIE, "hB")
comprueba(_compara(a5, solo_medios).returncode == 2,
          "si TODOS los bloques estan a medias, no hay comparacion que hacer")

# Y la trampa mas facil de colar: comparar solo los bloques comunes, no todos.
a4 = _corrida("a", "v0-baseline", {n: [3] * 4 for n in range(1, 11)}, SERIE, "hA")
b4 = _corrida("b", "cuellos", {n: [2] * 4 for n in range(1, 4)}, SERIE, "hB")
d4 = json.loads(_compara(a4, b4).stdout)
comprueba(d4["bloques_pareados"] == 3,
          "con 10 bloques en A y 3 en B solo se parean 3, no se promedia sobre 10")

print()
if fallos:
    print(f"{len(fallos)} fallos")
    sys.exit(1)
print("todos los tests del orquestador pasan")
