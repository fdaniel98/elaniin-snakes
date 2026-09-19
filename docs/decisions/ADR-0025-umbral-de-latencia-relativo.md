---
title: El umbral de latencia pasa a ser sobrecoste, no un numero absoluto
read_when: "antes de tocar los umbrales de /move de config/loop.json o el check 8"
authority: derived
source: torneo-v4-hojas (60 partidas) + scripts/smoke.py
last_verified: 2026-09-19
size_bytes: 4141
---

# ADR-0025 — El umbral de latencia pasa a ser sobrecoste {#adr-0025}

## Contexto {#adr-0025-contexto}

El check 8 exigia `p99 <= 50 ms` y `maximo <= 150 ms` en `POST /move`, umbrales que §4 del
prompt maestro fija y que se escribieron cuando `brain_v0` decidia en **100 us**. Con v4 el
check falla:

    FAIL p99 199.49ms > 50.0ms

Y no hay ningun defecto detras. Una busqueda anytime **gasta su presupuesto por diseño**:
el p50 medido en el torneo es 198 ms sobre un `max_compute_ms` de 200. Que tarde 199 ms es
exactamente lo que se le pide.

El problema del umbral absoluto es que medía la cosa equivocada. «Cuanto tarda la snake» no
predice un timeout del arbitro; lo que lo predice es **cuanto se pasa de lo que ella misma
se concedio**, porque el margen que queda hasta los 500 ms del arbitro es lo que absorbe la
red y los parones del sistema.

## Decision {#adr-0025-decision}

Los umbrales de `/move` en `config/loop.json` pasan a ser **sobrecoste sobre
`time.max_compute_ms`**, que `scripts/smoke.py` lee de `snake/config/default.json`:

| antes | ahora |
|---|---|
| `p99_move_ms_max: 50` | `p99_move_overhead_ms_max: 30` (smoke) + `p99_move_ms_max: 350` (techo duro) |
| `max_move_ms_max: 150` | `max_move_overhead_ms_max: 120` (smoke) + `max_move_ms_max: 350` (techo duro) |

Los dos absolutos **se conservan** con el valor que §4 declara como fallo duro (350 ms), y
son contra ellos que el check 9 valida las metricas del ledger. La primera version de este
cambio los quitaba, y con eso desactivaba esa comprobacion en silencio: lo cazo el veneno
`poison_9c`, que dejo de fallar donde debia. Es la razon de que el selftest exista.

Con el presupuesto actual de 200 ms eso da un p99 maximo de 230 y un maximo de 320. Medido
hoy: p99 199.6, maximo 201.7.

**Y un techo duro que ninguna configuracion puede saltarse.** Un umbral derivado de la
configuracion se defrauda subiendo la configuracion, asi que `smoke.py` falla si el umbral
derivado supera `500 - network_margin - safety_margin` (hoy 350 ms). Subir
`max_compute_ms` a 400 no relaja el check: lo rompe.

Aprobado explicitamente por el humano. `config/loop.json` es uno de los tres ficheros que
exigen aprobacion y ADR (regla de oro 10).

## Por que esto NO es relajar el gate {#adr-0025-no-es-relajar}

Es la pregunta que toca hacerse, porque el numero sube de 50 a 230. Tres razones:

1. **Comprueba algo que antes no se comprobaba.** El umbral viejo no tenia ninguna relacion
   con el presupuesto: con `max_compute_ms` a 350 y el umbral en 50, el check fallaba
   siempre, y con la snake vieja pasaba siempre por seis ordenes de magnitud de margen. En
   ningun caso medía la salud del servidor.
2. **Tiene techo duro.** El umbral no puede crecer indefinidamente con la configuracion.
3. **Tiene dos venenos nuevos** (`poison_8c`, `poison_8d`): un servidor que tarda 200 ms de
   mas y una configuracion que sube el presupuesto por encima del techo. Los dos tienen que
   hacer fallar el check 8, o el check se habria vuelto vacio al hacerse relativo, que es
   justo lo que la regla de oro 4 prohibe.

## Alternativas descartadas {#adr-0025-alternativas}

- **Subir el absoluto a 250 ms.** Funciona hoy y miente mañana: el dia que se cambie el
  presupuesto habria que acordarse de mover el umbral a mano, y no nos acordariamos.
- **Quitar la comprobacion de latencia del check 8.** Es lo unico que mide de punta a punta
  -servidor, JSON, red local- lo que el arbitro va a ver.
- **Medir solo el computo interno (`us=` del log).** Deja fuera el parseo de JSON y la pila
  de red, que son parte de lo que consume el timeout.

## Lo que sigue pendiente {#adr-0025-pendiente}

Los 30 y 120 ms de sobrecoste salen de lo medido en local, sin red. El numero que de verdad
los calibra es el p99 de RTT contra la URL desplegada, que es la DoD de la fase 7 y sigue
sin medirse.
