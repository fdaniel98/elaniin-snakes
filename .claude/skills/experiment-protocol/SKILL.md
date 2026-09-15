---
name: experiment-protocol
description: Como se corre un experimento valido en el Training Room. Usar al lanzar un A/B, al interpretar un resultado, al diseñar el campo de evaluacion, y cuando alguien afirme que una version es mejor que otra.
---

# Protocolo de experimentos

## Que dice el protocolo, y donde vive

Las reglas de aceptacion (unidad de analisis, partidas espejo, metrica primaria, que hacer
al agotar el presupuesto) tienen un unico dueño: ver docs/strategy.md#s-ab. Esta skill no
las repite; explica **por que** son asi, que es lo que se olvida.

- Por bloques y no por partida: en una partida de 4 las posiciones suman una constante, de
  modo que las partidas de un mismo bloque no son independientes.
- Espejo y no compartida: si A y B juegan la misma partida su covarianza es negativa por
  construccion, y el test exige independencia.
- Una sola metrica con veredicto: mirar diez a alfa 0.05 da un 40% de probabilidad de al
  menos un falso positivo.

## Semillas comunes que de verdad reducen varianza

El schedule de comida y hazards se **pre-genera** desde la semilla e indexado por turno,
antes de la partida. Si se genera bajo demanda, en cuanto A y B divergen en un movimiento
el consumo del stream de RNG diverge, y la reduccion de varianza se evapora justo en las
partidas donde hay diferencia.

## Decision

El test es SPRT sobre la diferencia pareada por bloque; alfa, beta, delta y `--max-games`
se declaran en el config y se escriben en el reporte. El veredicto y que hacer al agotar
el presupuesto: ver docs/strategy.md#s-ab. Wilson, solo para las tasas descriptivas.

Lo que hay que entender de esa regla: relanzar con otras semillas hasta que salga el
veredicto que uno queria es p-hacking, y por eso solo se amplia el mismo run.

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
