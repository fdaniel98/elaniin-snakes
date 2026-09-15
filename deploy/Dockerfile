# Multi-stage. Runtime distroless/cc porque distroless/static y distroless/base NO
# traen libstdc++; ademas se enlaza con -static-libstdc++ -static-libgcc para no
# depender de la version de libstdc++ de la imagen.
#
# La etapa de build usa debian:12, cuya glibc (2.36) es la misma que la del runtime
# distroless-cc-debian12: si la glibc de build fuera mayor, el binario no arrancaria.
#
# NO se inyecta CXXFLAGS (el check 7 del gate lo comprueba). La ISA la fija el preset
# `deploy`: -march=x86-64-v2 -mtune=generic. La ISA nativa esta prohibida aqui,
# ver docs/decisions/ADR-0004-deploy.md#d-0030.
# ver docs/decisions/ADR-0004-deploy.md

FROM debian:12 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ \
        cmake \
        ninja-build \
        nlohmann-json3-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Debian 12 trae GCC 12, no clang-18: se sobreescriben compilador y linker del preset
# por linea de comandos. Los flags de ISA y optimizacion siguen viniendo del preset.
RUN cmake --preset deploy \
        -DCMAKE_C_COMPILER=gcc \
        -DCMAKE_CXX_COMPILER=g++ \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    && cmake --build --preset deploy

FROM gcr.io/distroless/cc-debian12:nonroot

WORKDIR /app
COPY --from=build /src/build/deploy/bin/battlesnake-server /app/battlesnake-server
COPY --from=build /src/snake/config/default.json /app/snake/config/default.json

USER nonroot
EXPOSE 8080

# En distroless no hay shell, asi que no hay HEALTHCHECK: la sonda la hace la
# plataforma contra GET /health. ver docs/decisions/ADR-0004-deploy.md
ENTRYPOINT ["/app/battlesnake-server"]
