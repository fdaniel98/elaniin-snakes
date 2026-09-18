---
title: "ADR-0020: Cloud Run, con concurrencia 1 y CPU siempre asignada"
read_when: "antes de desplegar, de tocar deploy/cloud-run.sh o de discutir el coste del servicio"
authority: canonical
last_verified: 2026-09-18
size_bytes: 3470
---


## D-0190 Contexto {#d-0190}

El torneo es en dias y la snake tiene que estar viva en una URL publica. El contenedor ya
existe y el check 10 del gate lo construye y lo arranca en cada pasada: distroless,
`nonroot`, escucha en `0.0.0.0:$PORT`, `GET /` devuelve el JSON de personalizacion y
`GET /health` devuelve 200. Lo que faltaba no era ingenieria, era fijar la plataforma y
sus parametros, que §11 exige poner en un ADR porque «min-instances» y «concurrencia»
significan cosas distintas en cada proveedor.

## D-0191 Decision {#d-0191}

**Google Cloud Run**, region configurable, con estos cuatro parametros, que son los que
deciden si la snake llega a tiempo:

| Parametro | Valor | Por que |
|---|---|---|
| `--concurrency` | 1 | El cerebro usa el nucleo entero durante su deadline. Dos partidas en la misma instancia se roban CPU y las dos responden tarde |
| `--no-cpu-throttling` | activo | Sin esto la CPU se estrangula ENTRE requests. Entre dos movimientos pasan cientos de milisegundos, asi que la instancia llega fria a cada turno |
| `--min-instances` | 4 | Partidas simultaneas a cubrir sin arranque en frio. **Uno no basta** y esto es lo que cuesta dinero: es la decision de gasto del despliegue |
| `--timeout` | 30s | Es el timeout del SERVICIO, no el de la partida. Solo tiene que ser holgadamente mayor que los 500 ms del request |

`--cpu 1` porque el cerebro es de un solo hilo: dar mas vCPU no acelera nada hasta que
exista el paralelismo de v4. Subirlo antes es pagar por nada.

El script **verifica que esos flags existen** en el `gcloud` de quien despliega antes de
construir nada. Los nombres cambian entre versiones, y un deploy que falla a medias con
la imagen ya subida es peor que uno que no empieza.

La imagen se etiqueta con el commit **y el hash del config**: dos estrategias son binarios
distintos aunque el commit sea el mismo, y desplegar una creyendo desplegar la otra es el
error mas caro que se puede cometer el dia del torneo.

## D-0192 Alternativas descartadas {#d-0192}

| Alternativa | Por que no |
|---|---|
| Una VM (Compute Engine) | Control total del CPU y sin arranque en frio, a cambio de mantener maquina, parches y arranque. Para un servicio que responde un JSON pequeño es mucha operacion |
| Cloud Run con `min-instances 0` | Gratis mientras no juega, y una partida perdida por arranque en frio. El JSON de personalizacion llega tarde y el arbitro ni empieza la partida: paso dos veces en el torneo local |
| Cloud Run con concurrencia por defecto (80) | Una sola instancia atenderia varias partidas a la vez y el p99 lo decidiria el vecino |
| Kubernetes | Resuelve un problema de escala que no tenemos |

## D-0193 Consecuencias {#d-0193}

- **Coste:** cuatro instancias siempre encendidas con CPU asignada. Es la parte cara y es
  deliberada; bajarla es aceptar arranques en frio en partidas reales.
- El margen de red de 100 ms sigue siendo un **default, no una medicion**
  (ver docs/performance.md#p-07). Recalibrarlo pide el RTT real contra la URL desplegada,
  y eso es la DoD de la fase 7.
- La guia de despliegue vive en el propio script, no en un documento aparte: un comando
  que se copia mal es un torneo perdido, y el script se puede correr en seco.
