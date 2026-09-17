#!/usr/bin/env python3
"""Proxy grabador que se pone delante de una snake y anota lo que responde.

    recorder.py <puerto> <url_upstream> <archivo_de_log.jsonl>

El JSONL del arbitro oficial NO trae los movimientos: se derivan por diferencia de
cabezas entre turnos consecutivos, salvo para la serpiente que muere en ese turno, que
desaparece del board del turno siguiente. Es decir, el unico caso que el test
diferencial necesita verificar es el unico que el log no permite reconstruir.

Este proxy cierra ese agujero: reenvia la peticion tal cual y anota una linea por cada
POST /move con la respuesta CRUDA, su codigo HTTP y el tiempo transcurrido. La semantica
del arbitro ante una respuesta invalida o tardia (reenviar el LastMove anterior) NO se
aplica aqui: se aplica en el replay, que es quien la reproduce. ver docs/rules.md#r-03
"""

import json
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1])
UPSTREAM = sys.argv[2].rstrip("/")
LOG_PATH = sys.argv[3]

_log_lock = threading.Lock()


def _anota(registro):
    linea = json.dumps(registro, separators=(",", ":"), sort_keys=True)
    with _log_lock:
        with open(LOG_PATH, "a", encoding="utf-8") as destino:
            destino.write(linea + "\n")
            destino.flush()


class Grabador(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def _reenvia(self, cuerpo):
        peticion = urllib.request.Request(
            UPSTREAM + self.path,
            data=cuerpo,
            headers={"Content-Type": "application/json"},
            method=self.command,
        )
        inicio = time.monotonic()
        try:
            with urllib.request.urlopen(peticion, timeout=30) as respuesta:
                return respuesta.status, respuesta.read(), (time.monotonic() - inicio) * 1000.0
        except urllib.error.HTTPError as error:
            return error.code, error.read(), (time.monotonic() - inicio) * 1000.0
        except Exception as error:  # noqa: BLE001 - se anota, no se oculta
            return 0, str(error).encode("utf-8"), (time.monotonic() - inicio) * 1000.0

    def _responde(self, codigo, cuerpo):
        self.send_response(codigo if codigo else 502)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(cuerpo)))
        self.end_headers()
        self.wfile.write(cuerpo)

    def do_GET(self):  # noqa: N802 - lo impone BaseHTTPRequestHandler
        codigo, cuerpo, _ = self._reenvia(None)
        self._responde(codigo, cuerpo)

    def do_POST(self):  # noqa: N802 - lo impone BaseHTTPRequestHandler
        longitud = int(self.headers.get("Content-Length", "0"))
        cuerpo = self.rfile.read(longitud) if longitud else b""
        codigo, respuesta, ms = self._reenvia(cuerpo)

        if self.path.rstrip("/").endswith("/move") or self.path.rstrip("/") == "/move":
            try:
                peticion = json.loads(cuerpo)
                _anota(
                    {
                        "game": peticion["game"]["id"],
                        "turn": peticion["turn"],
                        "snake": peticion["you"]["id"],
                        "timeout": peticion["game"]["timeout"],
                        "status": codigo,
                        "elapsed_ms": round(ms, 3),
                        "body": respuesta.decode("utf-8", "replace"),
                    }
                )
            except (KeyError, ValueError) as error:
                _anota({"error": f"peticion /move no parseable: {error}"})

        self._responde(codigo, respuesta)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", PORT), Grabador).serve_forever()
