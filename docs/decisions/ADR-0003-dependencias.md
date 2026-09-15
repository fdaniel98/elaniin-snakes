---
title: "ADR-0003: cpp-httplib vendorizado, el resto desde el sistema"
read_when: "antes de añadir, quitar o cambiar una dependencia de terceros"
authority: canonical
last_verified: 2026-09-15
size_bytes: 1998
---


## D-0020 Contexto {#d-0020}

Hacen falta cuatro dependencias: tests (Catch2), benchmarks (Google Benchmark), JSON
(nlohmann) y servidor HTTP (cpp-httplib). El binario de produccion corre en una imagen
`distroless`, donde no hay gestor de paquetes ni shell.

## D-0021 Decision {#d-0021}

- Catch2, Google Benchmark y nlohmann/json entran como **paquetes del sistema**
  (`catch2`, `libbenchmark-dev`, `nlohmann-json3-dev`), que existen tanto en Ubuntu 24.04
  como en Debian 12.
- **cpp-httplib entra vendorizado** en `third_party/cpp-httplib/httplib.h` (MIT, v0.18.7),
  con su LICENSE al lado, e incluido como `SYSTEM` para que sus avisos no rompan `-Werror`.

## D-0022 Alternativas descartadas {#d-0022}

| Alternativa | Por que no |
|---|---|
| `libcpp-httplib-dev` de Debian | Esta empaquetado como **libreria compartida**, no como cabecera unica: el binario dependeria de `libcpp-httplib.so.0.14`, que no existe en `distroless/cc`. Habria que copiar la .so a la imagen y mantenerla sincronizada |
| `FetchContent` de CMake | Hace falta red en cada configuracion, incluida la de la imagen de build, y el gate dejaria de ser reproducible sin red |
| Vendorizar tambien nlohmann/json | Es un solo header, pero el paquete existe en ambas distribuciones y solo lo usa la frontera de JSON; vendorizar por vendorizar añade bytes al repo sin quitar riesgo |

## D-0023 Consecuencias {#d-0023}

- `scripts/bootstrap.sh` verifica las cabeceras del sistema, no solo los binarios.
- Actualizar cpp-httplib es un commit explicito que cambia un archivo conocido, y su
  version queda registrada en `docs/SOURCES.md`.
- El Dockerfile instala solo `nlohmann-json3-dev` en la etapa de build: Catch2 y Benchmark
  no hacen falta porque el preset `deploy` no compila tests ni benchmarks.
