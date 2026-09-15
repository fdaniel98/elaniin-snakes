---
title: ADR-0004: runtime distroless/cc con libstdc++ estatico
read_when: "antes de tocar deploy/Dockerfile o los flags del preset deploy"
authority: canonical
last_verified: 2026-09-15
size_bytes: 2334
---


## D-0030 Contexto {#d-0030}

El binario tiene que arrancar rapido, sin shell, con superficie minima, y ser portable
entre las CPU que use el proveedor.

## D-0031 Decision {#d-0031}

- Etapa de build sobre `debian:12` (glibc 2.36), runtime
  `gcr.io/distroless/cc-debian12:nonroot` con `USER nonroot`.
- Enlazado con `-static-libstdc++ -static-libgcc`.
- ISA: `-march=x86-64-v2 -mtune=generic` desde el preset `deploy`. **`-march=native` esta
  prohibido** y el check 7 lo verifica sobre los flags efectivos, no sobre el texto del
  preset, porque los presets heredan.
- En la imagen se compila con GCC 12 (lo que trae Debian 12) sobreescribiendo el
  compilador del preset por linea de comandos; los flags de ISA y optimizacion siguen
  viniendo del preset, y el Dockerfile **no** inyecta `CXXFLAGS`.

## D-0032 Alternativas descartadas {#d-0032}

| Alternativa | Por que no |
|---|---|
| `distroless/static` o `distroless/base` | No traen `libstdc++`: el binario no arranca |
| Binario estatico con musl | Portabilidad maxima, pero cambia el allocator y el comportamiento de hilos respecto a lo que se mide en desarrollo, y el p99 es justamente lo que estamos midiendo. Se reconsiderara si aparece un problema de arranque en frio |
| Sin `-march` | Sin linea base positiva de ISA el compilador genera para el x86-64 de 2003, sin POPCNT ni BMI1: `std::popcount` y `countr_zero`, de los que depende todo el bitboard, se compilan como bucle o tabla |
| `-march=x86-64-v3` | Solo si se documenta que la instancia destino garantiza AVX2; hoy no esta verificado |
| `HEALTHCHECK` en el Dockerfile | En distroless no hay shell; la sonda la hace la plataforma contra `GET /health` |

## D-0033 Consecuencias {#d-0033}

- La glibc de la etapa de build nunca puede ser mayor que la del runtime: ambas son
  Debian 12.
- Los numeros de `docs/performance.md` que sirvan de linea base se miden con el preset
  `bench-deployisa`, que usa la misma ISA que deploy.
- La guia de despliegue de la fase 7 fija `concurrency = 1`, `min-instances` >= 4 en
  torneo y CPU siempre asignada; el proveedor concreto se decidira en su propio ADR.
