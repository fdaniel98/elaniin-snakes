#!/usr/bin/env bash
# Verifica (y opcionalmente instala) el toolchain en el MISMO entorno en el que
# corren el gate y los hooks. Ver docs/decisions/ADR-0001-donde-corre-el-harness.md
#
#   ./scripts/bootstrap.sh            -> solo verifica; sale 1 si falta algo
#   ./scripts/bootstrap.sh --install  -> instala lo que falte con apt (necesita root o sudo)
set -uo pipefail

INSTALL=0
[[ "${1:-}" == "--install" ]] && INSTALL=1

APT_PACKAGES=(
    build-essential
    clang-18
    clang-format-18
    clang-tidy-18
    lld-18
    libclang-rt-18-dev
    cmake
    ninja-build
    jq
    git
    curl
    golang-go
    catch2
    libbenchmark-dev
    nlohmann-json3-dev
    libcpp-httplib-dev
)

missing=0
report() { printf '%-24s %-6s %s\n' "$1" "$2" "${3:-}"; }

need_cmd() {
    local cmd="$1" min="${2:-}"
    if command -v "$cmd" >/dev/null 2>&1; then
        report "$cmd" "OK" "$(command -v "$cmd")"
    else
        report "$cmd" "FALTA" "${min:+requiere $min}"
        missing=1
    fi
}

need_header() {
    local header="$1" pkg="$2" vendored="${3:-}"
    if [[ -f "/usr/include/$header" ]]; then
        report "$header" "OK" "/usr/include/$header"
    elif [[ -n "$vendored" && -f "$vendored" ]]; then
        # El build usa la copia del repo; exigir ademas la del sistema haria fallar el
        # bootstrap en una maquina limpia donde el build funciona.
        report "$header" "OK" "$vendored (vendorizado)"
    else
        report "$header" "FALTA" "apt install $pkg"
        missing=1
    fi
}

# El runtime de los sanitizers no es un binario ni una cabecera: sin el, el preset
# debug configura y enlaza mal, y el check 2 del gate muere en `cmake --preset debug`.
need_sanitizer_runtime() {
    local dir
    dir="$(clang++-18 -print-resource-dir 2>/dev/null)/lib/linux"
    if compgen -G "$dir/libclang_rt.asan-*.a" >/dev/null; then
        report "libclang_rt.asan" "OK" "$dir"
    else
        report "libclang_rt.asan" "FALTA" "apt install libclang-rt-18-dev"
        missing=1
    fi
}

echo "== entorno =="
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    report "so" "OK" "${PRETTY_NAME:-desconocido}"
fi
report "uname" "OK" "$(uname -sr)"
report "nproc" "OK" "$(nproc)"

if [[ $INSTALL -eq 1 ]]; then
    echo
    echo "== instalando =="
    SUDO=""
    [[ $EUID -ne 0 ]] && SUDO="sudo"
    DEBIAN_FRONTEND=noninteractive $SUDO apt-get update -qq || exit 2
    DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y "${APT_PACKAGES[@]}" || exit 2
fi

echo
echo "== herramientas de build =="
need_cmd clang++-18 "clang 17+"
need_cmd clang-18 "clang 17+"
need_cmd cmake "3.25+"
need_cmd ninja
need_cmd lld-18
need_sanitizer_runtime

echo
echo "== herramientas del gate y de los hooks =="
# Estas corren dentro de los hooks de .claude/settings.json: si faltan aqui, los
# hooks fallan en silencio aunque el build funcione.
need_cmd clang-format-18
need_cmd clang-tidy-18
need_cmd jq
need_cmd git
need_cmd curl

echo
echo "== dependencias de terceros =="
need_header "catch2/catch_test_macros.hpp" catch2
need_header "benchmark/benchmark.h" libbenchmark-dev
need_header "nlohmann/json.hpp" nlohmann-json3-dev
need_header "httplib.h" libcpp-httplib-dev "third_party/cpp-httplib/httplib.h"

echo
echo "== opcionales (fases posteriores) =="
if command -v go >/dev/null 2>&1; then
    report "go" "OK" "$(go version | awk '{print $3}') (CLI oficial, fase 3)"
else
    report "go" "AVISO" "sin go no se puede compilar battlesnake play (fase 3)"
fi

DOCKER_BIN=""
if command -v docker >/dev/null 2>&1 && docker version >/dev/null 2>&1; then
    DOCKER_BIN="docker"
elif command -v docker.exe >/dev/null 2>&1 && docker.exe version >/dev/null 2>&1; then
    DOCKER_BIN="docker.exe"
fi
if [[ -n "$DOCKER_BIN" ]]; then
    report "docker" "OK" "$DOCKER_BIN ($("$DOCKER_BIN" version --format '{{.Server.Version}}' 2>/dev/null))"
else
    report "docker" "AVISO" "el check 10 del gate (deploy real) quedara en SKIP"
fi

echo
echo "== version minima de cmake =="
cmake_ver="$(cmake --version 2>/dev/null | head -1 | awk '{print $3}')"
if [[ -n "$cmake_ver" ]]; then
    major="${cmake_ver%%.*}"
    rest="${cmake_ver#*.}"
    minor="${rest%%.*}"
    if (( major > 3 || (major == 3 && minor >= 25) )); then
        report "cmake>=3.25" "OK" "$cmake_ver"
    else
        report "cmake>=3.25" "FALTA" "encontrado $cmake_ver"
        missing=1
    fi
fi

echo
if [[ $missing -ne 0 ]]; then
    echo "FALTAN prerequisitos. Ejecuta: ./scripts/bootstrap.sh --install"
    exit 1
fi
echo "Toolchain completo. Siguiente: ./scripts/gate.sh"
