---
name: experiment-protocol
description: Como se corre un experimento valido en el Training Room. Usar al lanzar un A/B, al interpretar un resultado, al diseñar el campo de evaluacion, y cuando alguien afirme que una version es mejor que otra.
---

# Protocolo de experimentos

## Unidad de analisis

El **bloque**: una semilla por una rotacion de asientos. Nunca la partida. En una partida
de 4 las posiciones de los participantes suman una constante, asi que las partidas de un
mismo bloque no son independientes.

A y B **nunca juegan la misma partida**: se comparan en partidas espejo con identica
composicion de rivales y la misma semilla. Compartir partida hace su covarianza negativa
por construccion y viola la independencia que el test exige.

## Metrica primaria unica

Diferencia pareada de posicion media por bloque. Todo lo demas (win rate, causa de muerte,
turnos sobrevividos, latencia) es descriptivo y se reporta **sin p-valores ni veredicto**:
mirar diez metricas a alfa 0.05 da un 40% de probabilidad de al menos un falso positivo.

## Semillas comunes que de verdad reducen varianza

El schedule de comida y hazards se **pre-genera** desde la semilla e indexado por turno,
antes de la partida. Si se genera bajo demanda, en cuanto A y B divergen en un movimiento
el consumo del stream de RNG diverge, y la reduccion de varianza se evapora justo en las
partidas donde hay diferencia.

## Decision

SPRT sobre la diferencia pareada por bloque, con alfa, beta, delta y `--max-games`
**declarados en el config y escritos en el reporte**. Veredicto: `MEJORA`, `EMPEORA` o
`NO CONCLUYENTE`, mas los bloques consumidos. Intervalos de Wilson para las tasas
descriptivas.

Agotado `--max-games` sin cruzar frontera: `NO CONCLUYENTE`. **No se relanza con otro set
de semillas**; solo se amplia `--max-games` del mismo run. Relanzar hasta que salga es
p-hacking.

## Que invalida un resultado

- Presupuesto por reloj en vez de por nodos: una busqueda anytime devuelve movimientos
  distintos segun la carga de la maquina.
- Cambiar el campo congelado a mitad: crea `gauntlet-v2` e invalida toda comparacion con
  numeros del anterior.
- Contenedores con cuotas de CPU distintas, sin pinning, o con el arbitro compartiendo
  nucleo: el p99 pasa a medir el throttling, no el algoritmo.
- Comparar corridas con distinto numero de contenedores concurrentes, SMT o governor de
  frecuencia.
- Elegir la metrica despues de ver los datos.

## Calibracion del instrumento

Antes de creerse ningun A/B: test A/A (20 corridas con configs identicos y semillas
distintas; con alfa 0.05, cuatro o mas veredictos distintos de `NO CONCLUYENTE` es fallo
del harness) y regresion inyectada (un config degradado a proposito debe dar `EMPEORA`
antes de la mitad de `--max-games`).
