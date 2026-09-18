#!/usr/bin/env python3
"""Snake de caos: juega uniforme al azar y, a proposito, responde mal de vez en cuando.

    chaos_snake.py <puerto> <semilla> [--invalid P] [--slow P] [--slow-ms N] [--cauta]

No es una snake: es un generador de estados raros para el corpus del test diferencial.
Una snake que juega bien sobrevive y nunca produce choques cabeza a cabeza, autocolisiones
ni muertes contra la pared, que son justo las reglas que hay que verificar.

Las respuestas deliberadamente malas existen para ejercitar la rama del arbitro que el
motor propio tiene que reproducir: ante timeout, error HTTP, JSON invalido o direccion
desconocida el CLI reenvia el LastMove anterior. ver docs/rules.md#r-03

Con `--cauta` elige uniforme entre los movimientos que no son inmediatamente mortales en
vez de entre los cuatro. Sigue sin ser una snake -no busca comida ni espacio- pero
sobrevive, que es lo que hace falta cuando lo que se mide es el servidor propio: con
rivales suicidas la partida se acaba en siete turnos, no se llega al primer shrink y la
muestra de latencias es demasiado corta para que un p99 signifique algo.

Es determinista: la direccion depende solo de (semilla, game, turn, snake), asi que dos
corridas con la misma semilla producen la misma partida.
"""

import hashlib
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DIRECCIONES = ("up", "down", "left", "right")

PORT = int(sys.argv[1])
SEMILLA = sys.argv[2]
ARGS = sys.argv[3:]


def _opcion(nombre, defecto):
    if nombre in ARGS:
        return float(ARGS[ARGS.index(nombre) + 1])
    return defecto


CAUTA = "--cauta" in ARGS
PROB_INVALIDA = _opcion("--invalid", 0.0)
PROB_LENTA = _opcion("--slow", 0.0)
RETRASO_MS = _opcion("--slow-ms", 800.0)


def _casillas_ocupadas(board):
    """Casillas que seguiran ocupadas el proximo turno.

    Misma derivacion que el cerebro y que el smoke: la cola cuenta como libre salvo que
    los dos ultimos segmentos esten apilados. ver docs/rules.md#r-04
    """
    ocupadas = set()
    for snake in board["snakes"]:
        cuerpo = [(p["x"], p["y"]) for p in snake["body"]]
        apilada = len(cuerpo) >= 2 and cuerpo[-1] == cuerpo[-2]
        ultimo = len(cuerpo) - 1
        for i, casilla in enumerate(cuerpo):
            if i == ultimo and not apilada:
                continue
            ocupadas.add(casilla)
    return ocupadas


def _no_mortales(peticion):
    """Direcciones dentro del tablero que no chocan con nada que siga ocupado."""
    board = peticion["board"]
    yo = peticion["you"]
    cabeza = (yo["body"][0]["x"], yo["body"][0]["y"])
    ocupadas = _casillas_ocupadas(board)
    salida = []
    for nombre, (dx, dy) in (("up", (0, 1)), ("down", (0, -1)),
                             ("left", (-1, 0)), ("right", (1, 0))):
        destino = (cabeza[0] + dx, cabeza[1] + dy)
        if not (0 <= destino[0] < board["width"] and 0 <= destino[1] < board["height"]):
            continue
        if destino in ocupadas:
            continue
        salida.append(nombre)
    return salida


def _sorteo(*partes):
    """Valor en [0,1) derivado del hash de las partes. Sin RNG global: reproducible."""
    crudo = hashlib.sha256("|".join(str(p) for p in partes).encode("utf-8")).digest()
    return int.from_bytes(crudo[:8], "big") / float(1 << 64)


class Caos(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def _responde(self, codigo, cuerpo):
        octetos = cuerpo.encode("utf-8")
        self.send_response(codigo)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(octetos)))
        self.end_headers()
        self.wfile.write(octetos)

    def do_GET(self):  # noqa: N802 - lo impone BaseHTTPRequestHandler
        self._responde(
            200,
            json.dumps(
                {
                    "apiversion": "1",
                    "author": "battlesnake-royale",
                    "color": "#b0b0b0",
                    "head": "default",
                    "tail": "default",
                    "version": "chaos-1",
                }
            ),
        )

    def do_POST(self):  # noqa: N802 - lo impone BaseHTTPRequestHandler
        longitud = int(self.headers.get("Content-Length", "0"))
        crudo = self.rfile.read(longitud) if longitud else b"{}"

        if not self.path.rstrip("/").endswith("move"):
            self._responde(200, "{}")
            return

        peticion = json.loads(crudo)
        clave = (SEMILLA, peticion["game"]["id"], peticion["turn"], peticion["you"]["id"])

        if _sorteo("lenta", *clave) < PROB_LENTA:
            # Supera el timeout del arbitro a proposito: el CLI reenviara el LastMove.
            time.sleep(RETRASO_MS / 1000.0)

        if _sorteo("invalida", *clave) < PROB_INVALIDA:
            cual = int(_sorteo("cual", *clave) * 3)
            if cual == 0:
                self._responde(200, json.dumps({"move": "diagonal"}))
            elif cual == 1:
                self._responde(200, "{no es json")
            else:
                self._responde(500, json.dumps({"error": "caos"}))
            return

        candidatas = DIRECCIONES
        if CAUTA:
            seguras = _no_mortales(peticion)
            if seguras:
                candidatas = tuple(seguras)
        indice = int(_sorteo("dir", *clave) * len(candidatas))
        self._responde(200, json.dumps({"move": candidatas[indice], "shout": ""}))


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", PORT), Caos).serve_forever()
