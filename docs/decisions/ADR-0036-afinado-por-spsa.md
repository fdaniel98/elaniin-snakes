---
title: Los pesos se afinan con SPSA en la arena
read_when: "antes de tocar un peso de default.json a mano, o de cambiar el afinador"
authority: derived
source: training-room/afina.py y docs/experimentos.md
last_verified: 2026-09-20
size_bytes: 6153
---

# ADR-0036 — Afinado por SPSA {#adr-0036}

## D-0360 Contexto {#d-0360}

Los ~20 numeros de `snake/config/default.json` -120, 100, 80, 60, 30, 20, 10, 8, 6- los
puse a ojo y no se han tocado desde entonces. Cada version nueva añadio terminos; ninguna
reviso los que ya estaban. En un motor cuyo cuello de botella medido es la **evaluacion**
(ver docs/experimentos.md#exp-resumen), afinar pesos sin afinar es historicamente la
ganancia mas grande que queda sobre la mesa.

Por HTTP era imposible: miles de partidas. Con la arena calibrada, no.

## D-0361 La funcion objetivo, y por que sale a mitad de precio {#d-0361}

**Puesto medio de la candidata contra tres copias del campo, sobre una rotacion de
asientos completa.** Menos es mejor.

Y la referencia **no hay que jugarla**: con los cuatro contendientes iguales, las cuatro
partidas de un bloque son la misma partida y los puestos son 1, 2, 3 y 4, asi que el campo
saca **2.500 exacto, sin varianza ninguna**
(ver docs/experimentos.md#s-tercer-rival-r). Eso salio de un error -media corrida gastada
en medir una constante- y aqui se convierte en que cada evaluacion cuesta la mitad de lo
que costaria un A/B, y ademas se compara contra un numero sin ruido.

## D-0362 Decision {#d-0362}

**SPSA** (Spall): se perturban los veinte parametros a la vez con `+-c` y el gradiente se
estima de la diferencia entre las dos evaluaciones. **Dos evaluaciones por iteracion
cuesten los parametros que cuesten.** Una busqueda por coordenadas necesitaria dos por
parametro y no cabe en el tiempo disponible.

Tres detalles que no son accesorios:

- **Escala relativa.** Se optimiza `x_i = p_i / p_i inicial`, empezando en 1.0, con
  limites por parametro. Los pesos van de 2 a 120, y un paso absoluto que es un roce para
  `territory.weight` es un salto para `food.free_food_distance`.
- **`space.weight` queda FIJO como ancla.** Multiplicar todos los pesos por una constante
  casi no cambia nada: la busqueda compara valores entre si. Sin ancla, SPSA gastaria una
  dimension entera paseandose por esa escala.
- **Semillas comunes DENTRO de la iteracion, distintas entre iteraciones.** Las dos
  evaluaciones de una misma iteracion comparten bloques: ahi los numeros comunes son
  correctos y son lo que hace preciso el gradiente. Entre iteraciones, no. La primera
  version las compartia todas y el afinador se aprendio la muestra
  (ver docs/experimentos-afinado.md#s-afinado-control).

**Lo que NO se afina**, y no por olvido: `*.version`, `max_depth`, `max_rivals`,
`death_value`, `win_value`, `reserve_us`, `budget_nodes` y todo `time`. No son pesos de
evaluacion; son estructura y seguridad. Afinar `reserve_us` por puesto medio, por ejemplo,
lo empujaria a cero, porque en la arena el reloj no existe y en produccion es lo que evita
un timeout.

El generador de las perturbaciones es un Lehmer propio de 64 bits y no `random` de Python,
por la misma razon que el motor no usa las distribuciones de la STL: su algoritmo cambia
entre versiones y una corrida tiene que poder repetirse (ver docs/invariants.md#inv-08).

## D-0363 Lo que puede salir mal {#d-0363}

Se escribe antes de la primera corrida, como en las dos anteriores.

1. **El presupuesto corto no transfiere.** Se afina a ~1 000 nodos y se despliega a ~20 000
   (ver docs/performance.md#p-08). Un termino que compensa una busqueda superficial puede
   estorbar cuando la busqueda ve lo suficiente sola. Por eso la propuesta se verifica a
   presupuesto de despliegue ANTES de ir al gauntlet.
2. **Sobreajuste al campo.** El campo son copias de v5. Lo que salga sera bueno *contra
   v5*, que no es el gauntlet ni el torneo. Es la limitacion de ADR-0031 y no desaparece
   por afinar mas.
3. **Ruido mayor que la señal.** Con 32 partidas por evaluacion, el error tipico de la
   diferencia entre las dos ramas de una iteracion es del orden de la propia diferencia.
   SPSA lo tolera promediando a lo largo de las iteraciones, pero significa que una
   corrida corta puede terminar en un sitio peor que donde empezo.
4. **Sobreajuste a la muestra.** Este NO estaba en la lista y es el que paso. Esta
   corregido en D-0367.

## D-0364 Que pasa con la propuesta {#d-0364}

Nada entra en `default.json` por el afinado. La cadena es: propuesta -> A/B en arena
contra la base **con un campo distinto de las dos ramas** -> si gana, A/B por HTTP contra
`gauntlet-v1`. Solo entonces.

## D-0365 Alternativas descartadas {#d-0365}

| Alternativa | Por que no |
|---|---|
| Busqueda por coordenadas | Dos evaluaciones por parametro; con veinte no cabe en el tiempo |
| CMA-ES u otro de poblacion | Mas evaluaciones por generacion, y la ventaja se nota con presupuestos que aqui no hay |
| Afinar a presupuesto de despliegue | 20 veces mas caro por evaluacion; la corrida entera no cabria |
| Afinar contra el gauntlet | Es HTTP: miles de partidas serian semanas |
| Seguir poniendo los pesos a ojo | Es lo que llevamos haciendo, y es la palanca sin usar mas grande |

## D-0366 Estado {#d-0366}

**ACEPTADA. Primera corrida: SOBREAJUSTE, y el metodo corregido.**

80 iteraciones, 5 120 partidas, 106 minutos. Parecia bajar el puesto medio de 2.46 a 2.29;
en semillas frescas saco 2.4688, o sea nada (ver docs/experimentos-afinado.md#s-afinado-control).
La mejora entera era memoria de las 32 partidas con las que se afino.

## D-0367 Lo que se corrigio {#d-0367}

1. **Las semillas rotan entre iteraciones.** Numeros comunes dentro de la iteracion, donde
   son correctos; bloques propios en cada una, para que no haya muestra fija que aprender.
2. **Evaluacion de control obligatoria**, en semillas que ninguna iteracion toco, y ese es
   el numero que el script publica. Si no baja de 2.5, lo dice con todas las letras.
3. **La propuesta es el promedio de la cola de la trayectoria**, no el mejor punto visto:
   el mejor de N evaluaciones ruidosas esta sesgado a la baja por seleccion.
