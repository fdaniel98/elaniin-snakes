#!/usr/bin/env bash
# Check 11 del gate: el zoo nunca arranca codigo ajeno sin aislamiento.
#
# Se comprueba sobre el comando EFECTIVO que `zoo.sh up` construiria, no sobre un grep del
# texto del script: las banderas se montan en un array y un grep no ve si una se quedo
# fuera por una rama. `--dry-run` imprime ese comando exacto sin ejecutarlo, asi que esto
# corre igual en una maquina sin demonio de docker.
#
# No comprueba que el aislamiento funcione -eso lo dice el kernel cuando el contenedor
# arranca-, sino que se pide. Que una snake concreta sobreviva a `--read-only` se descubre
# construyendola, y ahi el fallo es ruidoso. ver zoo/README.md
set -uo pipefail

cd "$(dirname "$0")/.."

FALLOS=0
mal() {
    printf 'FAIL %s\n' "$*"
    FALLOS=$((FALLOS + 1))
}

OBLIGATORIAS=(
    '--user 65534:65534'
    '--read-only'
    '--cap-drop=ALL'
    '--security-opt=no-new-privileges'
    '--pids-limit=256'
)
# `-v` monta el disco del anfitrion dentro de codigo de terceros; el socket de docker es
# equivalente a ser root en la maquina; `--privileged` y `--network host` anulan el resto.
PROHIBIDAS=(
    ' -v '
    '--volume'
    'docker.sock'
    '--privileged'
    '--network host'
    '--network=host'
)

echo "== manifests =="
encontrados=0
for m in zoo/manifests/*.toml; do
    [[ -e "$m" ]] || continue
    encontrados=$((encontrados + 1))
    slug="$(basename "$m" .toml)"
    campo() { grep -E "^$1 *= *" "$m" | head -1 | sed -E 's/^[^=]*= *"?([^"]*)"?.*/\1/'; }

    [[ "$(campo sha)" =~ ^[0-9a-f]{40}$ ]] ||
        mal "$slug: sha no es un commit completo de 40 caracteres (una rama se mueve bajo los pies)"
    [[ -n "$(campo approved_by)" ]] ||
        mal "$slug: approved_by vacio; falta la confirmacion humana del repositorio"
    [[ -n "$(campo repo)" ]] || mal "$slug: sin repo"
    [[ "$(campo port)" =~ ^[0-9]+$ ]] || mal "$slug: port ausente o no numerico"
done
[[ $encontrados -gt 0 ]] || mal "no hay ningun manifest en zoo/manifests/: este check pasaria por vacio"
echo "OK   $encontrados manifests revisados"

echo "== comando efectivo de arranque =="
for m in zoo/manifests/*.toml; do
    [[ -e "$m" ]] || continue
    slug="$(basename "$m" .toml)"
    # stderr, que es por donde zoo.sh emite las lineas DRY.
    cmd="$(./scripts/zoo.sh up "$slug" --port 8199 --cpus 1 --cpuset 0 --memory 512m --dry-run 2>&1 >/dev/null |
        grep '^DRY docker run' || true)"
    if [[ -z "$cmd" ]]; then
        mal "$slug: 'zoo.sh up --dry-run' no imprimio ningun 'docker run'"
        continue
    fi
    for bandera in "${OBLIGATORIAS[@]}"; do
        [[ "$cmd" == *"$bandera"* ]] || mal "$slug: al arrancar falta $bandera"
    done
    for bandera in "${PROHIBIDAS[@]}"; do
        [[ "$cmd" == *"$bandera"* ]] && mal "$slug: al arrancar aparece $bandera"
    done
    # Publicar en todas las interfaces expone la snake de terceros a la red local.
    [[ "$cmd" == *'-p 127.0.0.1:'* ]] ||
        mal "$slug: el puerto no se publica en 127.0.0.1"
done
echo "OK   banderas de aislamiento presentes en cada arranque"

echo "== el clon de terceros no cae dentro del repo =="
# `docker build` compila codigo ajeno; si el clon viviera bajo el arbol acabaria en el
# indice de git o en un `docker build .` del contexto equivocado.
build_cmd="$(./scripts/zoo.sh build "$(basename "$(ls zoo/manifests/*.toml | head -1)" .toml)" --dry-run 2>&1 >/dev/null |
    grep '^DRY docker build' || true)"
if [[ -z "$build_cmd" ]]; then
    mal "'zoo.sh build --dry-run' no imprimio ningun 'docker build'"
else
    contexto="${build_cmd##* }"
    [[ "$contexto" == /* ]] || mal "el contexto de build no es una ruta absoluta: $contexto"
    case "$contexto" in
        "$PWD"/*) mal "el contexto de build cae dentro del repo: $contexto" ;;
    esac
fi
echo "OK   el clon va fuera del arbol"

if [[ $FALLOS -gt 0 ]]; then
    echo "check 11 FAIL: $FALLOS problemas"
    exit 1
fi
echo "check 11 PASS"
