#!/usr/bin/env python3
"""Tests del orquestador. Se corren solos:  python3 training-room/test_tr.py

No necesitan docker ni arbitro: lo que se prueba es la derivacion, que es donde estan los
errores que mienten en silencio. Que un contenedor no arranque se ve; que los puestos
esten mal repartidos, no.
"""
import importlib.util
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
ruta_log.write_text("""INFO Snake ID: id0 URL: http://127.0.0.1:9700, Name: "v0-baseline"
WARN Request to http://127.0.0.1:9700/move failed
ERROR context deadline exceeded
WARN Request to http://127.0.0.1:9700/end failed
ERROR context deadline exceeded
WARN Got non-ok status code from http://127.0.0.1:9701/otra/move
""", encoding="utf-8")
inc = tr.incidencias_de(ruta_log, "http://127.0.0.1:9700")
comprueba(inc["timeout"] == 1, "el timeout de /move cuenta una vez")
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

vacio = Path(tempfile.mkdtemp())
tr.abre_db(vacio / "torneo.sqlite").close()
comprueba(tr.cmd_reanaliza(types.SimpleNamespace(out=str(vacio))) == 1,
          "un recalculo que no modifica nada FALLA en vez de decir que hizo algo")

print()
if fallos:
    print(f"{len(fallos)} fallos")
    sys.exit(1)
print("todos los tests del orquestador pasan")
