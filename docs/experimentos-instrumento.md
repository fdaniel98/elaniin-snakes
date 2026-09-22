---
title: Fallos del instrumento de medicion
read_when: "cuando un A/B sale RAMAS IDENTICAS o raro, antes de fiarse del resultado"
authority: derived
source: docs/results/arena-* y training-room/compara.py
last_verified: 2026-09-23
size_bytes: 1848
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

### S-TERRITORIO-DUELO-R2 La corrida buena, y la sonda que mintio {#s-territorio-duelo-r2}

Repetido con el binario correcto: 1v1 estandar, v13 contra v5, 60 bloques, 14 821 nodos.

| | v5 | v13 |
|---|---|---|
| puesto medio | 1.500 | 1.575 |
| duelos ganados | 60 de 120 | 51 de 120 |
| turnos vividos | 274.7 | 305.6 |

Diferencia **+0.075** (del lado malo), IC95 [-0.015, +0.165]: NO CONCLUYENTE y probablemente
peor. **v13 no entra**; su royale ya no hace falta, porque falla el criterio del 1v1.

La leccion es de instrumento: la sonda previa -24 partidas con **3 000 nodos**- habia dado
1.375, a favor. Con el presupuesto real el signo se da la vuelta. Una sonda con 5 veces
menos nodos juega otro juego: sirve para descartar lo que rompe algo, no para elegir
candidata.
