---
title: "ADR-0001: el toolchain vive en WSL2 y el repo en el filesystem de Windows"
read_when: "al cambiar donde corren el gate, los hooks o el build"
authority: canonical
last_verified: 2026-09-15
size_bytes: 2443
---


## D-0001 Contexto {#d-0001}

El host es Windows 11. El proyecto exige ASan y UBSan con `-fno-sanitize-recover=all`,
`clang-tidy`, scripts POSIX y una imagen `distroless`: nada de eso es nativo en Windows.
La distro `Ubuntu` registrada en WSL2 estaba rota (su `ext4.vhdx` no existia), asi que el
punto de partida era: no habia compilador de C++ en ninguno de los dos entornos.

## D-0002 Decision {#d-0002}

Se instala `Ubuntu-24.04` como distro nueva (sin desregistrar la rota, que no tiene
datos) y **todo el toolchain vive ahi**. El repositorio se queda en el filesystem de
Windows, en `/mnt/c/...`, y el agente lo edita desde Windows.

Todo comando del harness se ejecuta con el prefijo literal
`wsl -d Ubuntu-24.04 -e bash -lc '...'`, y las reglas de permisos de
`.claude/settings.json` se escriben con ese prefijo porque casan por prefijo.

## D-0003 Alternativas descartadas {#d-0003}

| Alternativa | Por que no |
|---|---|
| Repo dentro del filesystem de Linux (`~/battlesnake`) | Es lo mas rapido en I/O, pero el usuario deja de ver y editar los archivos desde Windows, y las herramientas del agente tendrian que pasar por `wsl` para cada lectura. El coste de I/O del 9p es tolerable para un proyecto de este tamaño; si deja de serlo, se mide y se reabre este ADR |
| MSVC en Windows | No hay ASan+UBSan equivalente con `-fno-sanitize-recover=all`, ni `clang-tidy` con este ruleset, ni ruta razonable a `distroless` |
| Contenedor de desarrollo con Docker | Añade una capa mas sobre WSL2, que es donde ya corre el demonio de Docker |

## D-0004 Consecuencias {#d-0004}

- `.gitattributes` fuerza LF en la copia de trabajo: sin eso, los `.sh` fallan en WSL2 con
  `bad interpreter: ^M`, y `wc -c` daria distinto en Windows y en Linux, rompiendo el
  check de `size_bytes`.
- El check 0 del gate falla si algun `.sh` contiene CR.
- `scripts/bootstrap.sh` verifica el toolchain **dentro de WSL2**, que es donde corren los
  hooks, no solo donde corre el build.
- Docker Desktop no tiene activada la integracion WSL para esta distro, asi que `docker`
  no esta en el PATH de Linux. El gate y `bootstrap.sh` aceptan `docker.exe` como
  alternativa; si tampoco esta, el check 10 sale `SKIP` y el resumen lo dice.
