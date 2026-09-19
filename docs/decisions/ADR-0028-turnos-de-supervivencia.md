---
title: La salud se mide en turnos de vida, no en puntos
read_when: "antes de tocar los umbrales de salud, comida o hazard"
authority: derived
source: docs/results/torneo-v5-longitud (60 partidas, causas del final)
last_verified: 2026-09-19
size_bytes: 3398
---

# ADR-0028 — La salud se mide en turnos de vida {#adr-0028}

## Contexto {#adr-0028-contexto}

v5 gano su A/B y gana 26 partidas de 60. Las causas del final de las otras 34 invierten el
diagnostico con el que empezo el proyecto:

| causa | partidas |
|---|---|
| sobrevivio (gano) | 26 |
| **hazard con poca salud** | **15** |
| **hambre** | **12** |
| otra / eleccion | 3 |
| sin salida (encerrada) | 3 |
| zona de cabeza mas larga | 1 |

En v0 morian **132 de 178** sin ninguna salida. En v5, **3 de 34**: ese problema esta
resuelto. Lo que mata ahora es la salud, **27 de 34**. Y donde mas duele: de los 21
segundos puestos, **11 son hazard y 10 hambre**. Casi todo lo que se pierde por poco se
pierde por quedarse sin vida.

## El error de unidad {#adr-0028-unidad}

En Royale el hazard cuesta `hazardDamagePerTurn` **mas** el -1 de cada turno: con 14 de
daño son **15 de vida por turno**, o sea que dentro del hazard se muere en **~7 turnos**
desde salud llena. Todos nuestros umbrales estaban en salud ABSOLUTA:

- la comida se buscaba con `health <= 50`, que dentro del hazard son **3.3 turnos**:
  demasiado tarde para encontrarla;
- la penalizacion de hazard se multiplicaba solo con `turnos <= 2`, cuando ya no da tiempo
  ni a salir.

La salud absoluta es la unidad correcta en un tablero limpio y la equivocada en Royale,
donde el tablero se encoge y a partir del turno 180 casi todo es hazard. Justo cuando
vamos segundos peleando por ganar.

## Decision {#adr-0028-decision}

Con `survival.version >= 1`, la magnitud es
**`turnos_vida = salud / (1 + daño_si_estoy_en_hazard)`**:

1. **Valor del margen**, saturado en `safe_turns` (25). Con coste 1 son 25 de salud; dentro
   de un hazard de 14 son 375, o sea que **dentro del hazard nunca se esta comodo**, que es
   exactamente lo correcto.
2. **Castigo por el borde, CONTINUO** por debajo de `critical_turns` (5), en vez del
   escalon de v5.
3. **La comida se busca por debajo de `seek_below_turns` (15)**. En tablero limpio son 15
   de salud; dentro del hazard, salud 100 da 6.7 turnos, asi que ahi se busca **siempre**.
   Es el reloj real del juego.

El escalon del hazard desaparece **solo**: estar dentro ya hunde `turnos_vida` porque el
coste por turno entra en el calculo. Queda una penalizacion base plana por «estar ahi es
peor», sin multiplicadores.

## Coste, medido {#adr-0028-coste}

Cero. Profundidad media con 4 vivas y 200 ms de presupuesto: 10.89 sin, 10.92 con.

## Lo que puede salir mal {#adr-0028-riesgo}

El riesgo simetrico es volverse **glotona**: si `seek_below_turns` fuera muy alto, la snake
perseguiria comida con la vida llena y se metería en sitios por nada. Por eso el termino de
margen **satura**, y hay un test que exige que 90 de salud y 40 puntuen igual en tablero
limpio.

Si el A/B sale NO CONCLUYENTE, la lectura es que la causa de muerte no determina el puesto
-morir de hambre en segundo lugar puede ser el sintoma de ir ya perdiendo- y el siguiente
sitio donde mirar seria el medio juego y no el final.

## Estado {#adr-0028-estado}

**SIN MEDIR.** Se enciende con `snake/config/v6-turnos.json` y se mide CONTRA v5.
