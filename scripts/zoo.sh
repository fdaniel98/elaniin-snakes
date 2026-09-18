#!/usr/bin/env bash
# Construye y arranca las snakes publicas del zoo, aisladas y con recursos declarados.
#
#   ./scripts/zoo.sh list
#   ./scripts/zoo.sh build <slug>|--all
#   ./scripts/zoo.sh up <slug> --port N [--cpus C --cpuset S --memory M]
#   ./scripts/zoo.sh down <slug>|--all
#   ./scripts/zoo.sh check <slug>|--all
#   ./scripts/zoo.sh digest <slug>
#
# Todas aceptan --dry-run, que imprime el `docker ...` exacto sin ejecutarlo. Sirve para
# comprobar las banderas de aislamiento sin tener docker delante, que es la unica forma de
# revisar esto en una maquina sin demonio.
#
# NO se copia codigo de nadie. De cada snake publica se construye su imagen y se juega
# contra ella por HTTP: el contenedor es una caja negra. Lo unico que se ha leido de sus
# repositorios son hechos operativos -por que puerto sirven, como enrutan, si escriben al
# arrancar-, cada uno anotado en docs/SOURCES.md. Su estrategia no se lee ni se porta.
# ver zoo/README.md
#
# El aislamiento no es opcional: `docker build` compila codigo ajeno y `docker run` lo
# ejecuta. El clon va siempre a un directorio temporal fuera del repo, y el manifest fija
# el commit aprobado.
set -uo pipefail

cd "$(dirname "$0")/.."
RAIZ="$PWD"
MANIFIESTOS="$RAIZ/zoo/manifests"

DRY=0
ARGS=()
for a in "$@"; do
    if [[ "$a" == --dry-run ]]; then DRY=1; else ARGS+=("$a"); fi
done
set -- "${ARGS[@]+${ARGS[@]}}"

die() {
    printf 'ERROR %s\n' "$*" >&2
    exit 1
}

# ------------------------------------------------------------------ docker
DOCKER=""
buscar_docker() {
    [[ -n "$DOCKER" ]] && return 0
    for c in docker docker.exe; do
        if command -v "$c" >/dev/null 2>&1 && "$c" version >/dev/null 2>&1; then
            DOCKER="$c"
            return 0
        fi
    done
    return 1
}

# Ejecuta docker, o lo imprime si --dry-run. Devuelve 0 en seco para que el resto del
# flujo se pueda recorrer entero sin demonio.
#
# La linea DRY sale por **stderr** a proposito: las llamadas reales redirigen el stdout de
# docker a /dev/null, y por stdout la linea se perderia justo en los comandos que mas
# interesa revisar en seco, que son los que llevan las banderas de aislamiento.
docker_run() {
    if [[ $DRY -eq 1 ]]; then
        {
            printf 'DRY %s' "${DOCKER:-docker}"
            printf ' %q' "$@"
            printf '\n'
        } >&2
        return 0
    fi
    buscar_docker || die "sin docker utilizable (ni docker ni docker.exe responden)"
    "$DOCKER" "$@"
}

# ------------------------------------------------------------------ manifest
campo() { grep -E "^$2 *= *" "$1" | head -1 | sed -E 's/^[^=]*= *"?([^"]*)"?.*/\1/'; }

manifest_de() {
    local slug="$1" m="$MANIFIESTOS/$1.toml"
    [[ -f "$m" ]] || die "no existe $m. Un repo de terceros sin manifest propio no se construye."
    printf '%s\n' "$m"
}

# Un manifest que no pasa esto no se construye: sin commit fijo se estaria construyendo
# lo que haya hoy en una rama ajena, y sin approved_by no hay confirmacion humana del
# repositorio. ver zoo/README.md
valida_manifest() {
    local m="$1"
    local repo sha aprobado
    repo="$(campo "$m" repo)"
    sha="$(campo "$m" sha)"
    aprobado="$(campo "$m" approved_by)"
    [[ -n "$repo" ]] || die "$m sin repo"
    [[ "$sha" =~ ^[0-9a-f]{40}$ ]] || die "$m: sha tiene que ser un commit completo de 40 caracteres, no una rama"
    [[ -n "$aprobado" ]] || die "$m sin approved_by: falta la confirmacion humana del repositorio"
}

imagen_de() {
    local m sha
    m="$(manifest_de "$1")"
    sha="$(campo "$m" sha)"
    printf 'zoo/%s:%s\n' "$1" "${sha:0:12}"
}

slugs_todos() {
    local f
    for f in "$MANIFIESTOS"/*.toml; do
        [[ -e "$f" ]] || continue
        basename "$f" .toml
    done
}

# ------------------------------------------------------------------ list
cmd_list() {
    buscar_docker || true
    printf '%-28s %-14s %-9s %-9s %s\n' slug commit imagen arranca repo
    local slug m sha img estado corriendo
    for slug in $(slugs_todos); do
        m="$MANIFIESTOS/$slug.toml"
        sha="$(campo "$m" sha)"
        img="zoo/$slug:${sha:0:12}"
        estado="-"
        corriendo="-"
        if [[ -n "$DOCKER" ]]; then
            "$DOCKER" image inspect "$img" >/dev/null 2>&1 && estado="si" || estado="no"
            "$DOCKER" ps --filter "name=^zoo-$slug$" --format '{{.Names}}' 2>/dev/null |
                grep -q . && corriendo="si" || corriendo="no"
        fi
        printf '%-28s %-14s %-9s %-9s %s\n' "$slug" "${sha:0:12}" "$estado" "$corriendo" "$(campo "$m" repo)"
    done
    [[ -z "$DOCKER" ]] && echo "(sin docker: las columnas imagen y arranca quedan en '-')"
    return 0
}

# ------------------------------------------------------------------ build
cmd_build() {
    local objetivo="${1:-}"
    [[ -n "$objetivo" ]] || die "uso: zoo.sh build <slug>|--all"
    local lista
    if [[ "$objetivo" == --all ]]; then lista="$(slugs_todos)"; else lista="$objetivo"; fi

    local slug m repo sha dockerfile img tmp
    for slug in $lista; do
        m="$(manifest_de "$slug")"
        valida_manifest "$m"
        repo="$(campo "$m" repo)"
        sha="$(campo "$m" sha)"
        dockerfile="$(campo "$m" dockerfile)"
        [[ -n "$dockerfile" ]] || die "$m sin dockerfile: este repo necesita uno propio y no se ha escrito"
        img="zoo/$slug:${sha:0:12}"

        echo "== $slug =="
        if [[ -n "$DOCKER" || $DRY -eq 0 ]] && buscar_docker 2>/dev/null &&
            "$DOCKER" image inspect "$img" >/dev/null 2>&1; then
            echo "OK   $img ya existe; no se reconstruye"
            continue
        fi

        # Fuera del repo a proposito: es codigo de terceros y no puede acabar dentro del
        # arbol ni en su indice de git.
        tmp="$(mktemp -d)"
        if [[ $DRY -eq 1 ]]; then
            echo "DRY git clone --quiet $repo $tmp/src && git -C $tmp/src checkout $sha" >&2
        else
            git clone --quiet "$repo" "$tmp/src" || {
                rm -rf "$tmp"
                die "no se pudo clonar $repo"
            }
            git -C "$tmp/src" checkout --quiet "$sha" || {
                rm -rf "$tmp"
                die "el commit $sha no existe en $repo"
            }
        fi
        docker_run build -t "$img" -f "$tmp/src/${dockerfile#./}" "$tmp/src" || {
            rm -rf "$tmp"
            die "fallo el docker build de $slug"
        }
        rm -rf "$tmp"
        echo "OK   $img"
    done
}

# ------------------------------------------------------------------ digest
# El digest es de una imagen construida aqui, no de un registro: el gauntlet lo congela
# para detectar que la imagen cambio, no para poder descargarla.
cmd_digest() {
    local slug="${1:-}"
    [[ -n "$slug" ]] || die "uso: zoo.sh digest <slug>"
    local img
    img="$(imagen_de "$slug")"
    buscar_docker || die "sin docker utilizable"
    "$DOCKER" image inspect --format '{{.Id}}' "$img" 2>/dev/null ||
        die "no existe la imagen $img; corre 'zoo.sh build $slug'"
}

# ------------------------------------------------------------------ up
cmd_up() {
    local slug="${1:-}"
    shift || true
    [[ -n "$slug" ]] || die "uso: zoo.sh up <slug> --port N [--cpus C --cpuset S --memory M]"
    local puerto="" cpus="" cpuset="" memoria=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --port) puerto="${2:-}"; shift 2 ;;
            --cpus) cpus="${2:-}"; shift 2 ;;
            --cpuset) cpuset="${2:-}"; shift 2 ;;
            --memory) memoria="${2:-}"; shift 2 ;;
            *) die "opcion desconocida de up: $1" ;;
        esac
    done
    [[ -n "$puerto" ]] || die "up necesita --port"

    local m img puerto_snake entrypoint
    m="$(manifest_de "$slug")"
    valida_manifest "$m"
    img="$(imagen_de "$slug")"
    puerto_snake="$(campo "$m" port)"
    entrypoint="$(campo "$m" entrypoint)"
    [[ -n "$puerto_snake" ]] || die "$m sin port"

    # Las banderas de recursos son opcionales aqui y obligatorias en el torneo: quien mide
    # las pone, y sin ellas ningun numero de este zoo vale. ver zoo/README.md
    local recursos=()
    [[ -n "$cpus" ]] && recursos+=(--cpus "$cpus")
    [[ -n "$cpuset" ]] && recursos+=(--cpuset-cpus "$cpuset")
    [[ -n "$memoria" ]] && recursos+=(--memory "$memoria")

    docker_run run -d --name "zoo-$slug" \
        --user 65534:65534 \
        --read-only \
        --tmpfs /tmp \
        --cap-drop=ALL \
        --security-opt=no-new-privileges \
        --pids-limit=256 \
        "${recursos[@]+${recursos[@]}}" \
        -p "127.0.0.1:${puerto}:${puerto_snake}" \
        -e "PORT=${puerto_snake}" \
        "$img" >/dev/null || die "no arranco el contenedor de $slug"

    # `entrypoint` no es un binario: es el segmento de ruta con el que un servidor de
    # varias snakes elige cual responde. ver zoo/README.md
    local url="http://127.0.0.1:${puerto}${entrypoint:+/$entrypoint}"
    if [[ $DRY -eq 1 ]]; then
        echo "DRY url $url"
        return 0
    fi
    local listo=0 i
    for i in $(seq 1 120); do
        if curl -fsS "$url" >/dev/null 2>&1; then
            listo=1
            break
        fi
        sleep 0.5
    done
    if [[ $listo -eq 0 ]]; then
        "$DOCKER" logs "zoo-$slug" 2>&1 | tail -20 >&2
        cmd_down "$slug"
        die "$slug no respondio a GET $url en 60 s"
    fi
    printf '%s\n' "$url"
}

# ------------------------------------------------------------------ down
cmd_down() {
    local objetivo="${1:-}"
    [[ -n "$objetivo" ]] || die "uso: zoo.sh down <slug>|--all"
    local lista slug
    if [[ "$objetivo" == --all ]]; then lista="$(slugs_todos)"; else lista="$objetivo"; fi
    for slug in $lista; do
        docker_run rm -f "zoo-$slug" >/dev/null
    done
    return 0
}

# ------------------------------------------------------------------ check
# Arranca cada snake con el aislamiento completo, comprueba que responde, y apunta el
# digest de la imagen. Es lo unico que distingue "la imagen existe" de "la snake juega":
# una imagen puede construirse y luego no arrancar con --read-only, que es exactamente lo
# que le paso a Robosnake. ver zoo/README.md
#
# El resultado va a zoo/.estado-local.json, que NO se commitea: digests y salud son de
# esta maquina. Lo que si se congela, en gauntlet-v1.json, es el digest aprobado, y el
# orquestador aborta si el de la maquina no coincide.
cmd_check() {
    local objetivo="${1:-}"
    shift || true
    [[ -n "$objetivo" ]] || die "uso: zoo.sh check <slug>|--all [--cpus C --memory M]"
    local cpus=1 memoria=512m
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --cpus) cpus="${2:-}"; shift 2 ;;
            --memory) memoria="${2:-}"; shift 2 ;;
            *) die "opcion desconocida de check: $1" ;;
        esac
    done

    local lista
    if [[ "$objetivo" == --all ]]; then lista="$(slugs_todos)"; else lista="$objetivo"; fi

    buscar_docker || die "sin docker utilizable"
    local salida="zoo/.estado-local.json"
    local nucleos hilos_por_nucleo gobernador
    nucleos="$(nproc 2>/dev/null || echo 0)"
    hilos_por_nucleo="$(lscpu 2>/dev/null | sed -nE 's/^Thread\(s\) per core: *([0-9]+)/\1/p' | head -1)"
    gobernador="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo desconocido)"

    printf '{\n  "maquina": {"nucleos": %s, "hilos_por_nucleo": "%s", "gobernador": "%s"},\n  "snakes": [\n' \
        "$nucleos" "${hilos_por_nucleo:-desconocido}" "$gobernador" >"$salida"

    printf '%-28s %-9s %-9s %-13s %s\n' slug imagen responde aislamiento digest
    local primero=1 slug img digest url estado_img estado_resp aislamiento puerto=8190
    for slug in $lista; do
        estado_img=no; estado_resp=no; aislamiento=no; digest=""
        img="$(imagen_de "$slug")"
        if "$DOCKER" image inspect "$img" >/dev/null 2>&1; then
            estado_img=si
            digest="$("$DOCKER" image inspect --format '{{.Id}}' "$img")"
            "$DOCKER" rm -f "zoo-$slug" >/dev/null 2>&1
            # up ya aplica todas las banderas de aislamiento; si la snake no sobrevive a
            # ellas, aqui se ve, y ese es el punto.
            if url="$(cmd_up "$slug" --port "$puerto" --cpus "$cpus" --cpuset 0 --memory "$memoria" 2>/dev/null)"; then
                estado_resp=si
                aislamiento=ok
            fi
            cmd_down "$slug" >/dev/null 2>&1
            puerto=$((puerto + 1))
        fi
        printf '%-28s %-9s %-9s %-13s %s\n' "$slug" "$estado_img" "$estado_resp" "$aislamiento" "${digest:0:19}"
        [[ $primero -eq 0 ]] && printf ',\n' >>"$salida"
        primero=0
        printf '    {"slug": "%s", "imagen": "%s", "existe": "%s", "responde": "%s", "aislamiento": "%s", "digest": "%s"}' \
            "$slug" "$img" "$estado_img" "$estado_resp" "$aislamiento" "$digest" >>"$salida"
    done
    printf '\n  ]\n}\n' >>"$salida"
    echo
    echo "escrito $salida (no se commitea: es de esta maquina)"
    echo "nucleos=$nucleos hilos_por_nucleo=${hilos_por_nucleo:-desconocido} gobernador=$gobernador"
}

# ------------------------------------------------------------------ add
# No construye nada: escribe el manifest y para. Aprobar un repositorio es un acto humano,
# y aqui lo unico que se hace es dejarle el hueco donde firmarlo.
cmd_add() {
    local url="${1:-}" slug="${2:-}"
    [[ -n "$url" ]] || die "uso: zoo.sh add <repo-url> [slug]"
    [[ -n "$slug" ]] || slug="$(basename "$url" .git)"
    local destino="$MANIFIESTOS/$slug.toml"
    [[ -e "$destino" ]] && die "$destino ya existe"

    local tmp sha
    tmp="$(mktemp -d)"
    git clone --quiet --depth 1 "$url" "$tmp/src" || {
        rm -rf "$tmp"
        die "no se pudo clonar $url"
    }
    sha="$(git -C "$tmp/src" rev-parse HEAD)"
    local tiene_dockerfile=no
    [[ -f "$tmp/src/Dockerfile" ]] && tiene_dockerfile=si
    rm -rf "$tmp"

    mkdir -p "$MANIFIESTOS"
    cat >"$destino" <<TOML
# Manifest propio de una snake publica del zoo. ver zoo/README.md
#
# INCOMPLETO: aprobar un repositorio es confirmacion humana. Rellena approved_by y
# approved_at a mano, y solo despues de mirar que construye y que ejecuta ese commit.

name = "$slug"
slug = "$slug"
repo = "$url"
sha = "$sha"
dockerfile = "./Dockerfile"
port = 8080

approved_by = ""
approved_at = ""
TOML
    echo "escrito $destino"
    echo "commit al que apunta: $sha"
    [[ "$tiene_dockerfile" == no ]] &&
        echo "AVISO ese repo no trae Dockerfile en la raiz: hay que escribirle uno, o no se añade"
    echo "FALTA la confirmacion humana: approved_by esta vacio y zoo.sh se negara a construirlo"
}

# ------------------------------------------------------------------ main
case "${1:-}" in
    list) shift; cmd_list "$@" ;;
    build) shift; cmd_build "$@" ;;
    up) shift; cmd_up "$@" ;;
    down) shift; cmd_down "$@" ;;
    digest) shift; cmd_digest "$@" ;;
    check) shift; cmd_check "$@" ;;
    add) shift; cmd_add "$@" ;;
    *)
        sed -n '2,10p' "$0"
        exit 1
        ;;
esac
