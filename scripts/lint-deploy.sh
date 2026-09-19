#!/usr/bin/env bash
# Check 7 del gate: el preset `deploy` no puede llevar ISA nativa.
#
# Se comprueba sobre los FLAGS EFECTIVOS, no sobre el texto de CMakePresets.json: los
# presets heredan (`inherits`), asi que un `-march=native` heredado no aparece en el
# bloque `deploy` y el grep textual no lo veria.
#
# 1. Configura `deploy` en un directorio temporal.
# 2. Falla si -march/-mtune/-mcpu=native aparecen en compile_commands.json o en
#    CMAKE_CXX_FLAGS* de CMakeCache.txt.
# 3. Falla si el Dockerfile de deploy inyecta CXXFLAGS.
# 4. Grep textual como check redundante y barato.
set -uo pipefail

cd "$(dirname "$0")/.."

status=0
fail() {
    echo "FAIL $*"
    status=1
}

BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

echo "== configurando preset deploy en $BUILD_DIR =="
if ! cmake --preset deploy -B "$BUILD_DIR" >"$BUILD_DIR/configure.log" 2>&1; then
    echo "ERROR no se pudo configurar el preset deploy:"
    tail -20 "$BUILD_DIR/configure.log"
    exit 2
fi

# El patron empieza por '-': SIEMPRE se pasa tras '--', o grep lo interpreta como
# opcion (-m es max-count) y el check pasa sin comprobar nada.
NATIVE_RE='-m(arch|tune|cpu)=native'

echo "== flags efectivos =="
if [[ -f "$BUILD_DIR/compile_commands.json" ]]; then
    if grep -qE -- "$NATIVE_RE" "$BUILD_DIR/compile_commands.json"; then
        fail "compile_commands.json del preset deploy contiene ISA nativa:"
        grep -oE -- "$NATIVE_RE" "$BUILD_DIR/compile_commands.json" | sort -u
    else
        echo "OK   compile_commands.json sin ISA nativa"
    fi
else
    fail "el preset deploy no genero compile_commands.json"
fi

if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    if grep -E '^CMAKE_CXX_FLAGS' "$BUILD_DIR/CMakeCache.txt" | grep -qE -- "$NATIVE_RE"; then
        fail "CMAKE_CXX_FLAGS* del preset deploy contiene ISA nativa:"
        grep -E '^CMAKE_CXX_FLAGS' "$BUILD_DIR/CMakeCache.txt"
        status=1
    else
        echo "OK   CMAKE_CXX_FLAGS* sin ISA nativa"
    fi
    # La linea base positiva de ISA es obligatoria: sin -march, el compilador genera
    # para el x86-64 de 2003 y std::popcount se compila como bucle.
    if ! grep -E '^CMAKE_CXX_FLAGS:' "$BUILD_DIR/CMakeCache.txt" | grep -qE '\-march=x86-64-v[23]'; then
        fail "el preset deploy no fija una linea base de ISA (-march=x86-64-v2 o v3)"
    else
        echo "OK   linea base de ISA presente"
    fi
fi

echo "== Dockerfile =="
if [[ -f deploy/Dockerfile ]]; then
    if grep -qE '^(ENV|ARG)\s+(CXXFLAGS|CFLAGS)' deploy/Dockerfile; then
        fail "deploy/Dockerfile inyecta CXXFLAGS/CFLAGS"
    else
        echo "OK   el Dockerfile no inyecta CXXFLAGS"
    fi
    # Sin excepcion para comentarios: un comentario es una linea a un `sed -i` de
    # distancia de ser un flag. El Dockerfile remite al ADR en vez de nombrar la ISA.
    if grep -qE -- "$NATIVE_RE" deploy/Dockerfile; then
        fail "deploy/Dockerfile menciona ISA nativa"
    fi
else
    fail "no existe deploy/Dockerfile"
fi

# ------------------------------------------------------------------ compilador real
# El gate compila con clang y la imagen de deploy con GCC, asi que hasta ahora un error
# que solo GCC ve no aparecia hasta `docker build`, o sea el dia del despliegue. Paso de
# verdad: GCC rechaza `std::sort` sobre un array de 4 con -Warray-bounds -falso positivo
# del introsort de libstdc++- y el fallo salio al construir la imagen, no en el gate.
#
# Esto compila el SERVIDOR con el GCC que haya, que es barato y caza esa clase entera.
# No compila los tests a proposito: el Dockerfile tampoco, y el allocator instrumentado
# dispara un falso positivo de -Wmismatched-new-delete que no tiene que bloquear nada.
echo "== compila con GCC, que es lo que usa la imagen de deploy =="
# El `g++` del sistema primero, y las versiones concretas solo como respaldo. La imagen
# usa debian:12 (GCC 12); un g++ local mas nuevo es MAS estricto, y eso es lo que se
# quiere: la primera vez que esta comprobacion corrio con GCC 13 -en la maquina de
# referencia- encontro una escritura fuera de array que GCC 12 no ve
# (engine/src/rules.cpp, order_by_length). Preferir el viejo habria sido elegir no
# enterarse.
GXX="$(command -v g++ || command -v g++-12 || true)"
if [[ -z "$GXX" ]]; then
    echo "AVISO sin g++ en este entorno: el build de la imagen no queda cubierto aqui"
    echo "      (el check 10 lo construye de verdad y sigue siendo el arbitro)"
else
    TMP_GCC="$(mktemp -d)"
    trap 'rm -rf "$TMP_GCC"' EXIT
    if cmake -S . -B "$TMP_GCC" -GNinja -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_C_COMPILER="${GXX/g++/gcc}" -DCMAKE_CXX_COMPILER="$GXX" \
             >"$TMP_GCC/cmake.log" 2>&1 \
       && cmake --build "$TMP_GCC" --target battlesnake-server \
             >"$TMP_GCC/build.log" 2>&1; then
        echo "OK   battlesnake-server compila con $("$GXX" --version | head -1)"
        echo "     (la imagen usa debian:12/GCC 12; un g++ local mas nuevo es mas"
        echo "      estricto, y eso es intencionado)"
    else
        tail -30 "$TMP_GCC/build.log" 2>/dev/null || tail -20 "$TMP_GCC/cmake.log"
        fail "battlesnake-server NO compila con $GXX (la imagen de deploy fallaria)"
    fi
fi

echo "== grep textual (check redundante) =="
if grep -rnE -- "$NATIVE_RE" deploy/ 2>/dev/null; then
    fail "ISA nativa en deploy/"
else
    echo "OK   deploy/ sin ISA nativa"
fi

exit $status
