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
    # Se ignoran los comentarios: el propio Dockerfile documenta que -march=native esta
    # prohibido, y prohibir nombrarlo convertiria el check en un veto a la documentacion.
    if grep -vE '^[[:space:]]*#' deploy/Dockerfile | grep -qE -- "$NATIVE_RE"; then
        fail "deploy/Dockerfile menciona ISA nativa fuera de un comentario"
    fi
else
    fail "no existe deploy/Dockerfile"
fi

echo "== grep textual (check redundante) =="
if grep -rnE -- "$NATIVE_RE" deploy/ 2>/dev/null | grep -vE ':[[:space:]]*#'; then
    fail "ISA nativa en deploy/ fuera de un comentario"
else
    echo "OK   deploy/ sin ISA nativa"
fi

exit $status
