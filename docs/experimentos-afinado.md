---
title: Experimento de afinado por SPSA (v8), medido
read_when: "antes de volver a correr training-room/afina.py o de proponer otro afinado automatico"
authority: derived
source: docs/results/afinado-* y training-room/afina.py
last_verified: 2026-09-22
size_bytes: 4578
---

# Afinado por SPSA {#exp-afinado}

Sale de ver docs/experimentos.md#exp-resumen para que ese archivo quepa en su pack.

### S-AFINADO-R Resultado del afinado: hay señal, y no es la que imprimio el script {#s-afinado-r}

80 iteraciones de SPSA, 5 120 partidas, 1 000 nodos, 106 minutos
(ver docs/decisions/ADR-0036-afinado-por-spsa.md). El script imprimio **2.109** como mejor
puesto medio contra el 2.500 exacto del campo. **Ese numero esta sesgado y no se publica
como resultado**: es el MINIMO de 80 evaluaciones ruidosas, y el minimo de 80 sorteos cae
varios errores tipicos por debajo de la media aunque no hubiera mejorado nada.

Lo que si mide algo es la trayectoria, porque promedia:

| iteraciones | puesto medio |
|---|---:|
| 1-10 | 2.470 |
| 11-20 | 2.459 |
| 21-30 | 2.364 |
| 31-40 | 2.302 |
| 41-50 | 2.295 |
| 51-60 | 2.298 |
| 61-70 | 2.310 |
| 71-80 | **2.263** |

Media de las 20 primeras **2.4645**, de las 20 ultimas **2.2867**. Parecia una mejora de
-0.21, a casi ocho errores tipicos. **No lo era.**

El punto de llegada es estable: el mejor visto, el ultimo y el promedio de las 20 ultimas
iteraciones coinciden **dentro de +-0.05 por parametro**. No hay que elegir entre ellos.

**Lo que aprendio, que tiene sentido y no parece ruido:**

| parametro | v5 | v8 | lectura |
|---|---:|---:|---|
| `head.avoid_equal_or_longer` | 80 | **69** | menos miedo a las cabezas iguales o mayores |
| `head.prefer_shorter` | 8 | **9.5** | y mas ganas de ir a por las menores |
| `length.advantage_weight` | 60 | **72.9** | la longitud pesa aun mas de lo que pesaba |
| `length.hunt_weight` | 10 | **11.4** | y se persigue mas |
| `territory.hazard_value_pct` | 50 | **38** | el territorio dentro del hazard vale menos |
| `food.weight` | 6 | **5.6** | menos comida por comer |
| `food.seek_below_in_hazard` | 75 | **86** | pero comer MUCHO antes dentro del hazard |

Las dos ultimas juntas son lo interesante: el afinador no conoce el concepto de "turnos de
vida" que v6 intento meter a mano (ver docs/experimentos.md#s-supervivencia-r), y aun asi
llego solo a que dentro del hazard hay que comer antes. La hipotesis de v6 no era falsa;
estaba implementada en el sitio equivocado.

### El control lo tumba {#s-afinado-control}

Medido en semillas que el afinado no jugo nunca:

| donde se mide | partidas | v8 | v5 | mejora |
|---|---:|---:|---:|---:|
| semillas DEL AFINADO (base 1000) | 32 | **2.2188** | 2.5000 | -0.28 |
| semillas FRESCAS (base 50000) | 80 | **2.4688** | 2.5000 | **-0.03** |

Con 20 bloques el error tipico ronda 0.116, asi que 2.4688 es **indistinguible de 2.5**.
La mejora entera era memoria de 32 partidas concretas. **v8 NO entra.**

**La causa es una decision mia, y estaba escrita como si fuera una virtud.** El afinador
usaba los MISMOS 8 bloques en las 160 evaluaciones. Numeros aleatorios comunes son lo
correcto para **comparar** dos alternativas fijas -reducen la varianza de la diferencia- y
son una trampa para **optimizar**, porque el optimizador puede explotar una muestra que no
cambia nunca. Eso es exactamente lo que hizo: los pesos que encontro son buenos en esas 32
partidas y en ninguna otra.

Arreglado en `afina.py`, y en dos sitios:

1. **Las semillas rotan entre iteraciones.** Dentro de una iteracion las dos evaluaciones
   siguen compartiendolas -ahi los numeros comunes son correctos y no dejan nada que
   memorizar-, pero cada iteracion juega bloques propios.
2. **Evaluacion de control obligatoria al terminar**, en semillas que ninguna iteracion
   toco, y **ese** es el numero que se publica. Si no baja de 2.5, el script lo dice:
   `NO MEJORA: el control no baja de 2.5`.

Lo que costo saberlo: cuatro minutos. Por eso el paso de verificacion existe antes del
gauntlet y no despues.

**Un resultado colateral que si es bueno:** las 80 partidas de control dieron **2.4688
exacto** en la maquina de referencia y en el contenedor de desarrollo, que son maquinas
distintas con la mitad de nucleos una que la otra. Es la primera comprobacion de extremo a
extremo de que la arena es reproducible entre maquinas, que era una promesa del diseño
(ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0301) y hasta ahora solo una
promesa.

`snake/config/v8-afinado.json` se conserva como la evidencia del sobreajuste, no como
candidata.

