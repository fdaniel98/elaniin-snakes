#!/usr/bin/env bash
# Compila el arbitro oficial del SHA fijado en un entorno donde el proxy de modulos de
# Go esta bloqueado.
#
#   ./scripts/build-referee-mirrored.sh   -> build/tools/battlesnake
#
# No hace falta en la maquina de referencia: alli `go install ...@SHA` funciona y este
# script no se invoca. Existe porque el contenedor de la nube deniega proxy.golang.org y
# los dominios de vanidad golang.org/x, gopkg.in y go.uber.org, que es donde viven las
# dependencias INDIRECTAS del CLI (cobra, viper, fsnotify, websocket). El motor de
# reglas -- rules/, maps/, client/ -- sale del clon del SHA, sin espejo ni parche.
#
# Cada modulo se apunta a su espejo en GitHub en la MISMA version que declara el go.mod
# del SHA: no se fija ninguna version a mano. ver docs/SOURCES.md#s-02
set -uo pipefail

cd "$(dirname "$0")/.."
RAIZ="$PWD"
RULES_SHA="$(grep -oE '[0-9a-f]{40}' docs/SOURCES.md | head -1)"
SALIDA="$RAIZ/build/tools/battlesnake"

die() { echo "ERROR $*" >&2; exit 2; }
command -v go >/dev/null 2>&1 || die "falta go"

# El codigo de terceros se clona FUERA del arbol, como en scripts/zoo-game.sh.
TRABAJO="${TMPDIR:-/tmp}/battlesnake-referee-${RULES_SHA:0:12}"
mkdir -p "$TRABAJO" || die "no se pudo crear $TRABAJO"

if [[ ! -d "$TRABAJO/rules/.git" ]]; then
    git clone --quiet https://github.com/BattlesnakeOfficial/rules.git "$TRABAJO/rules" ||
        die "no se pudo clonar el motor de reglas"
fi
git -C "$TRABAJO/rules" checkout --quiet "$RULES_SHA" || die "el SHA $RULES_SHA no existe"

espejo() {
    case "$1" in
        golang.org/x/*) echo "https://github.com/golang/${1##*/}" ;;
        gopkg.in/yaml.v3) echo "https://github.com/go-yaml/yaml" ;;
        gopkg.in/ini.v1) echo "https://github.com/go-ini/ini" ;;
        go.uber.org/*) echo "https://github.com/uber-go/${1##*/}" ;;
        *) echo "" ;;
    esac
}

BLOQUEADOS="golang.org/x/net golang.org/x/sys golang.org/x/text gopkg.in/ini.v1 gopkg.in/yaml.v3 go.uber.org/multierr go.uber.org/atomic"

cd "$TRABAJO/rules" || die "no se pudo entrar en el clon"
for modulo in $BLOQUEADOS; do
    # La version sale del go.mod del SHA, no de este script.
    version="$(awk -v m="$modulo" '$1 == m {print $2; exit}' go.mod)"
    [[ -n "$version" ]] || continue
    destino="$TRABAJO/deps/${modulo//\//_}@${version}"
    if [[ ! -d "$destino" ]]; then
        url="$(espejo "$modulo")"
        [[ -n "$url" ]] || die "sin espejo conocido para $modulo"
        # gopkg.in/yaml.v3 y gopkg.in/ini.v1 etiquetan sin la parte .vN del path.
        etiqueta="$version"
        [[ "$modulo" == gopkg.in/yaml.v3 ]] && etiqueta="v3.0.1"
        git clone --quiet --depth 1 -b "$etiqueta" "$url" "$destino" ||
            die "no se pudo clonar el espejo de $modulo ($etiqueta)"
    fi
    # Algun tag antiguo no trae go.mod; sin el, `replace` por directorio no resuelve.
    if [[ ! -f "$destino/go.mod" ]]; then
        printf 'module %s\n\ngo 1.18\n' "$modulo" > "$destino/go.mod"
    fi
    declarado="$(awk '$1 == "module" {gsub(/"/, "", $2); print $2; exit}' "$destino/go.mod")"
    [[ "$declarado" == "$modulo" ]] ||
        die "el espejo de $modulo declara '$declarado': no es el mismo modulo"
    go mod edit -replace "${modulo}=${destino}" || die "no se pudo reescribir go.mod"
    echo "espejo  $modulo $version -> $destino"
done

mkdir -p "$RAIZ/build/tools"
GOFLAGS=-mod=mod GOPROXY=direct GOSUMDB=off GOPRIVATE='*' \
    go build -o "$SALIDA" ./cli/battlesnake || die "fallo el build del arbitro"

git -C "$TRABAJO/rules" checkout --quiet -- go.mod 2>/dev/null
echo "OK   $SALIDA"
"$SALIDA" --version 2>/dev/null || true
