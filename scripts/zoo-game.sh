#!/usr/bin/env bash
# Juega UNA partida completa del arbitro oficial entre nuestra v0 y una snake publica del
# zoo, y guarda su JSONL. Es el criterio 5 de la DoD de la fase 0.
#
#   ./scripts/zoo-game.sh [slug] [--seed N] [--keep]
#
# No es un torneo ni una medicion: una sola partida, para demostrar que la snake juega de
# principio a fin contra codigo que no es nuestro. Los recursos justos y comparables
# (--cpus, --cpuset-cpus, pinning) son de la fase 3, cuando exista el Training Room; aqui
# dan igual porque no se publica ningun numero. ver zoo/README.md
#
# Todo lo que sabe del arbitro esta verificado contra el Go del SHA fijado en
# docs/SOURCES.md, no supuesto: ver docs/rules-parametros.md#r-20
set -uo pipefail

cd "$(dirname "$0")/.."

SLUG="${1:-eremetic-eric}"
[[ "$SLUG" == --* ]] && SLUG="eremetic-eric"
SEED=""
KEEP=0
for arg in "$@"; do
    case "$arg" in
        --seed) NEXT_IS_SEED=1 ;;
        --keep) KEEP=1 ;;
        *)
            if [[ "${NEXT_IS_SEED:-0}" == "1" ]]; then
                SEED="$arg"
                NEXT_IS_SEED=0
            fi
            ;;
    esac
done
[[ -z "$SEED" ]] && SEED="$(date +%s)"

MANIFEST="zoo/manifests/${SLUG}.toml"
RULES_SHA="$(grep -oE '[0-9a-f]{40}' docs/SOURCES.md | head -1)"
OUR_PORT=8080
RESULTS="docs/results"

die() {
    echo "ERROR $*" >&2
    exit 1
}

field() { grep -E "^$1 *= *" "$MANIFEST" | head -1 | sed -E 's/^[^=]*= *"?([^"]*)"?.*/\1/'; }

# ---------------------------------------------------------------- 1. manifest
[[ -f "$MANIFEST" ]] || die "no existe $MANIFEST. Un repo de terceros sin manifest propio no se construye."

REPO="$(field repo)"
SHA="$(field sha)"
DOCKERFILE="$(field dockerfile)"
ENTRYPOINT="$(field entrypoint)"
SNAKE_PORT="$(field port)"
NAME="$(field name)"
APPROVED_BY="$(field approved_by)"

[[ -n "$REPO" && -n "$SHA" ]] || die "$MANIFEST sin repo o sin sha"
[[ ${#SHA} -eq 40 ]] || die "$MANIFEST: el sha tiene que ser un commit completo de 40 caracteres, no una rama"
# `docker build` ejecuta codigo arbitrario del repo. La confirmacion humana se registra en
# el manifest, una vez por repositorio, junto al commit aprobado.
[[ -n "$APPROVED_BY" ]] || die "$MANIFEST sin approved_by: falta la confirmacion humana del repositorio"

echo "== $NAME ($SLUG) =="
echo "repo:   $REPO"
echo "commit: $SHA (aprobado por $APPROVED_BY)"
echo "semilla de la partida: $SEED"

# ---------------------------------------------------------------- 2. docker
DOCKER=""
for candidate in docker docker.exe; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" version >/dev/null 2>&1; then
        DOCKER="$candidate"
        break
    fi
done
[[ -n "$DOCKER" ]] || die "sin docker utilizable (ni docker ni docker.exe responden)"

# ---------------------------------------------------------------- 3. arbitro oficial
CLI="$(command -v battlesnake || true)"
[[ -z "$CLI" && -x "$(go env GOPATH 2>/dev/null)/bin/battlesnake" ]] && CLI="$(go env GOPATH)/bin/battlesnake"
if [[ -z "$CLI" ]]; then
    command -v go >/dev/null 2>&1 || die "falta el CLI oficial y no hay go para compilarlo"
    echo "-- compilando el arbitro oficial en el SHA de docs/SOURCES.md --"
    go install "github.com/BattlesnakeOfficial/rules/cli/battlesnake@${RULES_SHA}" ||
        die "no se pudo compilar el arbitro"
    CLI="$(go env GOPATH)/bin/battlesnake"
fi
[[ -x "$CLI" ]] || die "el arbitro no es ejecutable: $CLI"

# ---------------------------------------------------------------- 4. nuestra snake
BINARY=build/release/bin/battlesnake-server
[[ -x "$BINARY" ]] || die "falta $BINARY; corre 'cmake --build --preset release'"

CONTAINER="zoo-${SLUG}-$$"
IMAGE="zoo/${SLUG}:${SHA:0:12}"
WORKDIR=""
SERVER_PID=""

cleanup() {
    [[ -n "$SERVER_PID" ]] && kill "$SERVER_PID" 2>/dev/null
    "$DOCKER" rm -f "$CONTAINER" >/dev/null 2>&1
    if [[ -n "$WORKDIR" && $KEEP -eq 0 ]]; then
        rm -rf "$WORKDIR"
    elif [[ -n "$WORKDIR" ]]; then
        echo "clon conservado en $WORKDIR"
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------- 5. build aislado
# Fuera del repo a proposito: lo que se clona es codigo de terceros y no puede acabar
# dentro del arbol ni en su indice de git.
WORKDIR="$(mktemp -d)"
echo "-- clonando en $WORKDIR --"
git clone --quiet "$REPO" "$WORKDIR/src" || die "no se pudo clonar $REPO"
git -C "$WORKDIR/src" checkout --quiet "$SHA" || die "el commit $SHA no existe en $REPO"

echo "-- docker build (puede tardar mucho: es codigo ajeno y se compila entero) --"
"$DOCKER" build -t "$IMAGE" -f "$WORKDIR/src/${DOCKERFILE#./}" "$WORKDIR/src" ||
    die "fallo el docker build de $SLUG"

# ---------------------------------------------------------------- 6. run aislado
HOST_PORT=8100
echo "-- arrancando el contenedor con el aislamiento de zoo/README.md --"
"$DOCKER" run -d --name "$CONTAINER" \
    --user 65534:65534 \
    --read-only \
    --tmpfs /tmp \
    --cap-drop=ALL \
    --security-opt=no-new-privileges \
    --pids-limit=256 \
    -p "127.0.0.1:${HOST_PORT}:${SNAKE_PORT}" \
    -e "PORT=${SNAKE_PORT}" \
    "$IMAGE" >/dev/null || die "no arranco el contenedor"

# `entrypoint` del manifest NO es un binario: es el segmento de ruta con el que el
# servidor elige que snake responde (web-axum/src/main.rs:170 de coreyja/battlesnake-rs
# enruta `/:snake_name`). Las snakes de un solo cerebro no lo traen y sirven en la raiz.
ZOO_URL="http://127.0.0.1:${HOST_PORT}${ENTRYPOINT:+/$ENTRYPOINT}"

ready=0
for _ in $(seq 1 60); do
    if curl -fsS "$ZOO_URL" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 0.5
done
if [[ $ready -eq 0 ]]; then
    echo "-- log del contenedor --"
    "$DOCKER" logs "$CONTAINER" 2>&1 | tail -20
    die "la snake del zoo no respondio a GET $ZOO_URL en 30 s"
fi
echo "OK   $NAME responde en $ZOO_URL"

# ---------------------------------------------------------------- 7. nuestra snake
PORT=$OUR_PORT "$BINARY" >"${RESULTS}/.zoo-game-server.log" 2>&1 &
SERVER_PID=$!
ready=0
for _ in $(seq 1 50); do
    curl -fsS "http://127.0.0.1:${OUR_PORT}/health" >/dev/null 2>&1 && {
        ready=1
        break
    }
    sleep 0.2
done
[[ $ready -eq 1 ]] || die "nuestro servidor no respondio en /health"
echo "OK   v0-baseline responde en 127.0.0.1:${OUR_PORT}"

# ---------------------------------------------------------------- 8. la partida
mkdir -p "$RESULTS"
OUT="${RESULTS}/$(date -u +%Y-%m-%d)-v0-vs-${SLUG}.jsonl"

# Flags verificados en cli/commands/play.go del SHA de docs/SOURCES.md, no supuestos:
#   -W/-H tablero, -g ruleset, -m mapa, -t timeout en ms, -r semilla, -o salida JSONL.
echo "-- jugando --"
"$CLI" play \
    -W 11 -H 11 \
    -g royale -m royale \
    -t 500 \
    -r "$SEED" \
    --name "v0-baseline" --url "http://127.0.0.1:${OUR_PORT}" \
    --name "$NAME" --url "$ZOO_URL" \
    -o "$OUT" 2>&1 | tail -15
play_status=${PIPESTATUS[0]}

[[ -s "$OUT" ]] || die "la partida no dejo JSONL en $OUT"
turns="$(wc -l <"$OUT")"
echo
echo "JSONL:  $OUT ($turns lineas)"
echo "semilla: $SEED"
rm -f "${RESULTS}/.zoo-game-server.log"
exit $play_status
