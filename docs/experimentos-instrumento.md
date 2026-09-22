---
title: Fallos del instrumento de medicion
read_when: "cuando un A/B sale RAMAS IDENTICAS o raro, antes de fiarse del resultado"
authority: derived
source: docs/results/arena-* y training-room/compara.py
last_verified: 2026-09-23
size_bytes: 1069
---

# Fallos del instrumento {#exp-instrumento}

Corridas que no midieron lo que decian medir, y el check que lo impide ahora.

### S-TERRITORIO-DUELO-R Primera corrida invalida: el binario no sabia leer el config {#s-territorio-duelo-r}

El primer A/B de v13 en 1v1 (60 bloques) salio **RAMAS IDENTICAS**: las mismas 120
partidas, las mismas causas y turnos que la linea base de v11. En este arbol v13 si cambia
decisiones, asi que la corrida uso un binario sin `duel.territory_version`, y el cargador
**ignoraba en silencio las claves que no conocia**. No mide nada; se repite.

Arreglado: `snake::unknown_keys()` lista las claves que el binario no lee; `arena_torneo`
**aborta** si un config trae alguna y el servidor emite `WARN=clave_desconocida`. Un test
exige que ningun config del repo tenga claves huerfanas.
