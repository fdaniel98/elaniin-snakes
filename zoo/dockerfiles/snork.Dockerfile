# Receta PROPIA para L4r0x/snork, que no trae Dockerfile. Se construye con el contexto del
# clon ajeno (commit fijado en zoo/manifests/snork-*.toml) pero con esta receta, que es la
# que se revisa. No copia codigo de snork a este repositorio: solo lo compila.
#
# El repo no versiona Cargo.lock, asi que las dependencias se resuelven al construir. La
# imagen resultante la congela el digest del gauntlet, no esta receta.
#
# Hechos operativos, leidos de src/bin/server.rs en el commit fijado:
#   --host  por defecto 127.0.0.1:5001 -> aqui 0.0.0.0:5001 para que el puerto publicado llegue
#   --latency ms restados del timeout (100 por defecto)
#   --config JSON con el agente: {"Flood":{}} | {"Tree":{}} | {"Mobility":{}}
# El agente lo elige la variable AGENT, que pone `zoo.sh up` desde el manifest.
FROM rust:1.85-bookworm AS build
WORKDIR /src
COPY . .
RUN cargo build --release --bin server

FROM debian:12-slim
COPY --from=build /src/target/release/server /usr/local/bin/snork-server
ENV AGENT=Flood
EXPOSE 5001
ENTRYPOINT ["/bin/sh", "-c", "exec /usr/local/bin/snork-server --host 0.0.0.0:5001 --config \"{\\\"${AGENT}\\\":{}}\""]
