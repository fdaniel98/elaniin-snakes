#!/usr/bin/env bash
# ORACULO UNICO de "hecho". Si el gate pasa y sabes que algo esta mal, el bug esta en el
# gate: se AÑADE el check que falta. Debilitarlo (comentar checks, `|| true`, reducir
# fixtures, relajar clang-tidy) exige aprobacion humana explicita y un ADR.
# ver docs/harness.md#h-01
#
#   ./scripts/gate.sh          todos los checks; es lo unico que cierra una fase
#   ./scripts/gate.sh --fast   checks 0,1,3(release),4,6,7,9,11,12; NO cierra fase
#
# Salida: una linea `CHECK <n> <nombre> PASS|FAIL|SKIP <segundos>` por check.
# Codigos: 0 = PASA, 1 = fallo de check, 2 = error de entorno o toolchain.
set -uo pipefail

cd "$(dirname "$0")/.."

FAST=0
[[ "${1:-}" == "--fast" ]] && FAST=1

PASSED=0
FAILED=0
SKIPPED=0
FAILED_NAMES=()

LOG_DIR="$(mktemp -d)"
trap 'rm -rf "$LOG_DIR"' EXIT

# Sanitizers: sin -fno-sanitize-recover=all (en el preset) y sin halt_on_error, UBSan
# reporta y continua, el test sale 0 y el gate pasaria con UB confirmado.
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:detect_stack_use_after_return=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

report() {
    local number="$1" name="$2" result="$3" seconds="$4"
    printf 'CHECK %s %s %s %s\n' "$number" "$name" "$result" "$seconds"
    case "$result" in
        PASS) PASSED=$((PASSED + 1)) ;;
        FAIL)
            FAILED=$((FAILED + 1))
            FAILED_NAMES+=("$number $name")
            ;;
        SKIP) SKIPPED=$((SKIPPED + 1)) ;;
    esac
}

run_check() {
    local number="$1" name="$2"
    shift 2
    local log="$LOG_DIR/check-$number.log"
    local started
    started="$(date +%s)"
    if "$@" >"$log" 2>&1; then
        report "$number" "$name" PASS "$(($(date +%s) - started))"
        return 0
    fi
    report "$number" "$name" FAIL "$(($(date +%s) - started))"
    echo "--- salida del check $number ($name) ---"
    # Primero las lineas de fallo: un check ruidoso puede empujar su propio diagnostico
    # fuera de la cola, y entonces el gate dice que fallo sin decir por que.
    if grep -qE '^(FAIL|ERROR)' "$log"; then
        grep -E '^(FAIL|ERROR)' "$log" | head -20
        echo "--- contexto (cola del log) ---"
    fi
    tail -20 "$log"
    echo "--- fin del check $number ---"
    return 1
}

skip_check() {
    report "$1" "$2" SKIP 0
}

# ---------------------------------------------------------------- check 0
check_scripts_hygiene() {
    local bad=0
    while read -r script; do
        if grep -qU $'\r' "$script" 2>/dev/null; then
            echo "FAIL $script contiene CR (rompe el shebang en WSL2)"
            bad=1
        fi
        if [[ ! -x "$script" ]]; then
            echo "FAIL $script no es ejecutable"
            bad=1
        fi
    done < <(find scripts -name '*.sh' | sort)

    # El bit del INDICE, no solo el del disco: con `core.fileMode=false` -que es lo
    # normal en un repo editado desde Windows- un `chmod +x` no llega al commit, y el
    # script sale sin bit en cualquier clon limpio. En /mnt/c todo se ve 777 y el
    # `-x` de arriba no lo caza nunca.
    local entry mode file
    while read -r entry; do
        mode="${entry%% *}"
        file="${entry#*$'\t'}"
        if [[ "$mode" != "100755" ]]; then
            echo "FAIL $file esta en el indice como $mode; usa 'git update-index --chmod=+x $file'"
            bad=1
        fi
    done < <(git ls-files -s 'scripts/*.sh' 'scripts/*.py' 2>/dev/null)

    [[ $bad -eq 0 ]] && echo "OK scripts sin CR y ejecutables, en disco y en el indice"
    return $bad
}

# ---------------------------------------------------------------- checks 1-3
build_release() {
    cmake --preset release && cmake --build --preset release
}

build_debug() {
    cmake --preset debug && cmake --build --preset debug
}

run_tests_release() {
    ctest --preset release --output-on-failure
}

run_tests_debug() {
    ctest --preset debug --output-on-failure
}

# ---------------------------------------------------------------- check 4
check_format() {
    local files
    files="$(find engine snake tests bench -name '*.cpp' -o -name '*.hpp' 2>/dev/null | sort)"
    [[ -z "$files" ]] && {
        echo "FAIL no hay fuentes que formatear"
        return 1
    }
    # shellcheck disable=SC2086
    clang-format-18 --dry-run --Werror $files
}

# ---------------------------------------------------------------- check 5
check_tidy() {
    local status=0
    local db=build/release/compile_commands.json
    if [[ ! -f "$db" ]]; then
        echo "FAIL falta $db (hace falta configurar el preset release antes)"
        return 1
    fi

    local files
    files="$(find engine/src snake/src -name '*.cpp' | sort)"
    # shellcheck disable=SC2086
    clang-tidy-18 -p build/release --warnings-as-errors='*' $files || status=1

    # Generadores de la STL cuyo algoritmo no esta especificado: romperian la
    # reproducibilidad de la arena entre libstdc++ y libc++. ver docs/invariants.md#inv-08
    #
    # Se mira TODO el arbol de engine/ y arena/, comentarios incluidos: un comentario
    # que nombre el generador es la via mas facil de que reaparezca en el codigo. La
    # documentacion que necesite nombrarlos vive fuera de esos dos directorios.
    local forbidden='std::(uniform_int_distribution|shuffle|sample|random_device)'
    if grep -rnE "$forbidden" engine arena 2>/dev/null; then
        echo "FAIL generador de la STL prohibido bajo engine/ o arena/"
        status=1
    else
        echo "OK sin generadores prohibidos bajo engine/ ni arena/"
    fi
    return $status
}

# ---------------------------------------------------------------- check 8
check_smoke() {
    local binary=build/release/bin/battlesnake-server
    if [[ ! -x "$binary" ]]; then
        echo "FAIL falta $binary"
        return 1
    fi

    local port=8099
    PORT=$port "$binary" >"$LOG_DIR/server.log" 2>&1 &
    local server_pid=$!
    local ready=0
    for _ in $(seq 1 50); do
        if curl -fsS "http://127.0.0.1:$port/health" >/dev/null 2>&1; then
            ready=1
            break
        fi
        sleep 0.2
    done
    if [[ $ready -eq 0 ]]; then
        echo "FAIL el servidor no respondio en /health"
        kill "$server_pid" 2>/dev/null
        return 1
    fi

    local status=0
    curl -fsS "http://127.0.0.1:$port/" | grep -q '"apiversion"' || {
        echo "FAIL GET / no devuelve el JSON de personalizacion"
        status=1
    }
    local code
    code="$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$port/no-existe")"
    [[ "$code" == "404" ]] || {
        echo "FAIL una ruta desconocida devuelve $code, se esperaba 404"
        status=1
    }

    python3 scripts/smoke.py --url "http://127.0.0.1:$port" \
        --json-out "$LOG_DIR/smoke.json" || status=1

    kill "$server_pid" 2>/dev/null
    wait "$server_pid" 2>/dev/null
    return $status
}

# ---------------------------------------------------------------- check 10
docker_bin() {
    if command -v docker >/dev/null 2>&1 && docker version >/dev/null 2>&1; then
        echo docker
    elif command -v docker.exe >/dev/null 2>&1 && docker.exe version >/dev/null 2>&1; then
        echo docker.exe
    fi
}

check_deploy() {
    local docker
    docker="$(docker_bin)"
    [[ -z "$docker" ]] && {
        echo "sin docker"
        return 3
    }

    local tag="battlesnake-royale:gate"
    "$docker" build -f deploy/Dockerfile -t "$tag" . || return 1

    local name="battlesnake-gate-$$"
    "$docker" run -d --rm --name "$name" -p 18080:8080 "$tag" >/dev/null || return 1

    local status=0
    local ready=0
    for _ in $(seq 1 50); do
        if curl -fsS "http://127.0.0.1:18080/health" >/dev/null 2>&1; then
            ready=1
            break
        fi
        sleep 0.2
    done
    if [[ $ready -eq 0 ]]; then
        echo "FAIL el contenedor no respondio en /health"
        "$docker" logs "$name" 2>&1 | tail -20
        status=1
    else
        curl -fsS "http://127.0.0.1:18080/" | grep -q '"apiversion"' || {
            echo "FAIL GET / del contenedor no devuelve personalizacion"
            status=1
        }
        echo "OK imagen construida y contenedor respondiendo"
    fi

    "$docker" stop "$name" >/dev/null 2>&1
    return $status
}

# ---------------------------------------------------------------- ejecucion
echo "== gate ($([[ $FAST -eq 1 ]] && echo rapido || echo completo)) =="

# Guard de entorno. Media docena de checks llaman a git (ls-files, cat-file, archive,
# merge-base). Si git rechaza el repositorio -lo normal al trabajar desde /mnt/c, donde
# el propietario de los archivos no es quien ejecuta- todos ellos fallan y el gate acusa
# al codigo de cosas que no pasan. Eso es un error de ENTORNO: salida 2, no 1.
if ! git rev-parse HEAD >/dev/null 2>&1; then
    echo "ERROR de entorno: git no puede leer este repositorio." >&2
    git rev-parse HEAD 2>&1 | sed 's/^/  /' >&2
    echo "  Si el mensaje habla de 'dubious ownership', ejecuta una vez:" >&2
    printf "    git config --global --add safe.directory '%s'\n" "$PWD" >&2
    exit 2
fi

run_check 0 higiene-scripts check_scripts_hygiene || true
[[ $FAILED -gt 0 ]] && {
    echo "$PASSED checks PASS, 0 tests PENDIENTES"
    exit 1
}

run_check 1 build-release build_release || {
    echo "$PASSED checks PASS, 0 tests PENDIENTES"
    exit 1
}

if [[ $FAST -eq 1 ]]; then
    skip_check 2 build-debug
else
    run_check 2 build-debug build_debug || {
        echo "$PASSED checks PASS, 0 tests PENDIENTES"
        exit 1
    }
fi

if [[ $FAST -eq 1 ]]; then
    run_check 3 tests-release run_tests_release || true
else
    run_check 3 tests-release run_tests_release || true
    run_check 3 tests-debug run_tests_debug || true
fi

run_check 4 clang-format check_format || true

if [[ $FAST -eq 1 ]]; then
    skip_check 5 clang-tidy
else
    run_check 5 clang-tidy check_tidy || true
fi

run_check 6 lint-docs ./scripts/lint-docs.sh || true
run_check 7 lint-deploy ./scripts/lint-deploy.sh || true

if [[ $FAST -eq 1 ]]; then
    skip_check 8 smoke-e2e
else
    run_check 8 smoke-e2e check_smoke || true
fi

if [[ $FAST -eq 1 ]]; then
    run_check 9 lint-loop ./scripts/loop_verify.sh --fast || true
else
    run_check 9 lint-loop ./scripts/loop_verify.sh || true
fi

if [[ $FAST -eq 1 ]]; then
    skip_check 10 deploy-real
else
    started="$(date +%s)"
    if [[ -n "$(docker_bin)" ]]; then
        if check_deploy >"$LOG_DIR/check-10.log" 2>&1; then
            report 10 deploy-real PASS "$(($(date +%s) - started))"
        else
            report 10 deploy-real FAIL "$(($(date +%s) - started))"
            tail -30 "$LOG_DIR/check-10.log"
        fi
    else
        # Sin docker el veredicto no es sobre el codigo: se marca SKIP y el resumen lo
        # dice, para que nadie lea un gate verde como "el deploy esta probado".
        report 10 deploy-real SKIP 0
    fi
fi

# Barato y sin docker: se comprueba el comando que `zoo.sh up` construiria, en seco, asi
# que corre tambien en --fast.
run_check 11 lint-zoo ./scripts/lint-zoo.sh || true
# Los tests del orquestador no necesitan docker ni arbitro: prueban la derivacion, que es
# donde un error miente en silencio. Que un contenedor no arranque ya se ve solo.
run_check 12 tests-training-room python3 training-room/test_tr.py || true

PENDING=0
if [[ -x build/release/bin/unit_tests ]]; then
    PENDING="$(build/release/bin/unit_tests '[.pending]' --list-tests 2>/dev/null |
        grep -cE '^  [A-Za-z]' || echo 0)"
fi

echo
if [[ $FAST -eq 1 ]]; then
    echo "MODO RAPIDO - no apto para cerrar fase"
fi
if [[ $SKIPPED -gt 0 ]]; then
    echo "$SKIPPED checks SKIP"
fi
echo "$PASSED checks PASS, $PENDING tests PENDIENTES"

if [[ $FAILED -gt 0 ]]; then
    echo "checks fallidos: ${FAILED_NAMES[*]}"
    exit 1
fi
exit 0
