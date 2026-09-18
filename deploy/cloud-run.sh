#!/usr/bin/env bash
# Construye, sube y despliega la snake en Cloud Run.
#
#   ./deploy/cloud-run.sh --proyecto MI_PROYECTO [--region us-central1] [--config default]
#   ./deploy/cloud-run.sh --proyecto MI_PROYECTO --config v1 --servicio snake-v1
#
# Con --dry-run imprime los comandos exactos y no ejecuta nada.
#
# Todo lo que decide este script esta razonado en
# ver docs/decisions/ADR-0020-cloud-run.md, y los cuatro parametros que de verdad
# importan son estos:
#
#   --concurrency 1       Un request a la vez por instancia. El cerebro usa el nucleo
#                         entero durante su deadline; dos partidas en la misma instancia
#                         se roban CPU y las dos pierden.
#   --no-cpu-throttling   CPU siempre asignada. Sin esto Cloud Run estrangula la CPU
#                         ENTRE requests, y el primer movimiento de cada turno llega tarde.
#   --min-instances N     N = partidas simultaneas a cubrir sin arranque en frio. En
#                         torneo son >= 4. Uno solo NO basta, y cuesta dinero: es la
#                         decision de gasto de este despliegue.
#   --timeout 30s         El timeout del SERVICIO, no el de la partida. Tiene que ser
#                         holgadamente mayor que los 500 ms del request.
set -uo pipefail

cd "$(dirname "$0")/.."

PROYECTO=""
REGION="us-central1"
CONFIG="default"
SERVICIO=""
MIN_INSTANCIAS=4
CPUS=1
MEMORIA=512Mi
REPO=battlesnake
DRY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --proyecto) PROYECTO="${2:-}"; shift 2 ;;
        --region) REGION="${2:-}"; shift 2 ;;
        --config) CONFIG="${2:-}"; shift 2 ;;
        --servicio) SERVICIO="${2:-}"; shift 2 ;;
        --min-instancias) MIN_INSTANCIAS="${2:-}"; shift 2 ;;
        --cpus) CPUS="${2:-}"; shift 2 ;;
        --memoria) MEMORIA="${2:-}"; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        *) echo "opcion desconocida: $1" >&2; exit 2 ;;
    esac
done

die() {
    printf 'ERROR %s\n' "$*" >&2
    exit 1
}

[[ -n "$PROYECTO" ]] || die "falta --proyecto"
[[ -f "snake/config/${CONFIG}.json" ]] ||
    die "no existe snake/config/${CONFIG}.json (hay: $(ls snake/config/*.json | xargs -n1 basename | tr '\n' ' '))"
[[ -n "$SERVICIO" ]] || SERVICIO="battlesnake-${CONFIG}"

COMMIT="$(git rev-parse --short=12 HEAD 2>/dev/null || echo sin-git)"
HASH_CFG="$(sha256sum "snake/config/${CONFIG}.json" | cut -c1-16)"
# La etiqueta lleva commit Y hash del config: dos estrategias son binarios distintos
# aunque el commit sea el mismo, y desplegar una creyendo desplegar la otra es el error
# mas caro posible el dia del torneo.
IMAGEN="${REGION}-docker.pkg.dev/${PROYECTO}/${REPO}/snake:${COMMIT}-${HASH_CFG}"

ejecuta() {
    if [[ $DRY -eq 1 ]]; then
        printf 'DRY'
        printf ' %q' "$@"
        printf '\n'
        return 0
    fi
    "$@"
}

# Los nombres de los flags de gcloud cambian entre versiones y este script no los puede
# dar por buenos: si uno no existe, el deploy fallaria a medias, con la imagen ya subida.
# ver docs/decisions/ADR-0020-cloud-run.md
if [[ $DRY -eq 0 ]]; then
    AYUDA="$(gcloud run deploy --help 2>/dev/null || true)"
    if [[ -n "$AYUDA" ]]; then
        for flag in --concurrency --no-cpu-throttling --min-instances --allow-unauthenticated --timeout; do
            grep -q -- "$flag" <<<"$AYUDA" ||
                die "tu gcloud no conoce $flag; comprueba 'gcloud run deploy --help' y avisa antes de seguir"
        done
        echo "OK   los cinco flags que importan existen en tu gcloud"
    else
        echo "AVISO no pude leer 'gcloud run deploy --help'; no se han verificado los flags"
    fi
fi

echo "proyecto:  $PROYECTO"
echo "region:    $REGION"
echo "config:    snake/config/${CONFIG}.json (hash $HASH_CFG)"
echo "servicio:  $SERVICIO"
echo "imagen:    $IMAGEN"
echo

# ------------------------------------------------------------------ 1. repositorio
echo "== 1/4 repositorio de imagenes =="
if [[ $DRY -eq 0 ]] && gcloud artifacts repositories describe "$REPO" \
    --location="$REGION" --project="$PROYECTO" >/dev/null 2>&1; then
    echo "OK   $REPO ya existe"
else
    ejecuta gcloud artifacts repositories create "$REPO" \
        --repository-format=docker --location="$REGION" --project="$PROYECTO" \
        --description="Battlesnake" || echo "(si ya existia, esto es inofensivo)"
fi
ejecuta gcloud auth configure-docker "${REGION}-docker.pkg.dev" --quiet

# ------------------------------------------------------------------ 2. build
echo
echo "== 2/4 build =="
# El Dockerfile copia snake/config/default.json. Para desplegar otra estrategia se copia
# ese config al sitio durante el build y se restaura despues, igual que hace el torneo:
# asi la imagen lleva dentro exactamente el config cuyo hash va en la etiqueta.
RESTAURAR=0
if [[ "$CONFIG" != "default" ]]; then
    cp snake/config/default.json /tmp/default.json.bak
    cp "snake/config/${CONFIG}.json" snake/config/default.json
    RESTAURAR=1
fi
ejecuta docker build -f deploy/Dockerfile -t "$IMAGEN" .
RC=$?
if [[ $RESTAURAR -eq 1 ]]; then
    cp /tmp/default.json.bak snake/config/default.json
fi
[[ $RC -eq 0 ]] || die "fallo el docker build"

# ------------------------------------------------------------------ 3. push
echo
echo "== 3/4 push =="
ejecuta docker push "$IMAGEN" || die "fallo el push"

# ------------------------------------------------------------------ 4. deploy
echo
echo "== 4/4 deploy =="
ejecuta gcloud run deploy "$SERVICIO" \
    --image="$IMAGEN" \
    --region="$REGION" \
    --project="$PROYECTO" \
    --platform=managed \
    --allow-unauthenticated \
    --concurrency=1 \
    --cpu="$CPUS" \
    --memory="$MEMORIA" \
    --min-instances="$MIN_INSTANCIAS" \
    --max-instances=10 \
    --no-cpu-throttling \
    --timeout=30s \
    --port=8080 \
    --labels=config="$CONFIG",commit="$COMMIT" \
    --quiet || die "fallo el deploy"

[[ $DRY -eq 1 ]] && exit 0

URL="$(gcloud run services describe "$SERVICIO" --region="$REGION" \
    --project="$PROYECTO" --format='value(status.url)')"
echo
echo "URL: $URL"
echo
echo "== comprobacion =="
# Las dos que pide el sitio de Battlesnake, mas la sonda.
codigo="$(curl -s -o /tmp/raiz.json -w '%{http_code}' "$URL/")"
echo "GET /        $codigo   $(head -c 120 /tmp/raiz.json)"
echo "GET /health  $(curl -s -o /dev/null -w '%{http_code}' "$URL/health")"
echo
echo "Registra esta URL en play.battlesnake.com: $URL"
echo
echo "Antes del torneo, mide el RTT real desde fuera: es lo que recalibra el margen de"
echo "red de 100 ms, que hoy es un default y no una medicion."
