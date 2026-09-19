#!/usr/bin/env bash
# Check 9 del gate: lint del ledger del loop de ingenieria.
#
# Para cada entregable declarado en el bloque loop-deliverables de STATE.md exige un
# ledger .loop/<fase>/<slug>.ledger.json y verifica, parando en el primer fallo, los
# diez puntos de la especificacion del loop. ver docs/harness.md#h-05
#
#   --fast   valida el ledger sin re-ejecutar benchmarks (NUNCA se omite por completo)
#
# Ningun umbral esta hardcodeado aqui: todos se leen de config/loop.json, y el ledger
# debe traer el sha256 de ese mismo archivo.
set -uo pipefail

cd "$(dirname "$0")/.."

FAST=0
if [[ "${1:-}" == "--fast" ]]; then
    FAST=1
fi
# El flag existe por simetria con gate.sh --fast. Este script nunca re-ejecuta
# benchmarks: valida el ledger y los artefactos ya escritos, asi que --fast no omite
# ninguna comprobacion. Se parsea para que no se cuele como argumento desconocido.
readonly FAST

CONFIG=config/loop.json
STATE=STATE.md

command -v jq >/dev/null 2>&1 || {
    echo "ERROR falta jq"
    exit 2
}

[[ -f "$CONFIG" ]] || {
    echo "FAIL falta $CONFIG"
    exit 1
}
[[ -f "$STATE" ]] || {
    echo "FAIL falta $STATE"
    exit 1
}

CONFIG_SHA="$(sha256sum "$CONFIG" | awk '{print $1}')"
PHASE="$(grep -m1 -E '^Fase:' "$STATE" | grep -oE '[0-9]+' | head -1)"
if [[ -z "$PHASE" ]]; then
    echo "FAIL $STATE no declara 'Fase: <n>'"
    exit 1
fi

# Entregables: filas `| slug | archivo | clases |` del bloque marcado.
mapfile -t DELIVERABLES < <(
    awk '/<!-- BEGIN:loop-deliverables -->/{inside=1; next}
         /<!-- END:loop-deliverables -->/{inside=0}
         inside && /^\| / && !/^\| *slug/ && !/^\|[-| ]*\|$/ {print}' "$STATE"
)

if [[ ${#DELIVERABLES[@]} -eq 0 ]]; then
    # Un check 9 que pasa por no encontrar ledgers es un fallo de fase, no un exito.
    echo "FAIL $STATE no declara ningun entregable con loop en el bloque loop-deliverables"
    exit 1
fi

MIN_ITER="$(jq -r '.closing.min_iterations' "$CONFIG")"
MAX_ITER="$(jq -r '.closing.max_iterations' "$CONFIG")"
CLEAN_TAIL="$(jq -r '.closing.clean_tail_iterations' "$CONFIG")"
DISCARDED_MAX="$(jq -r '.closing.discarded_ratio_max' "$CONFIG")"

status=0
fail() {
    echo "FAIL $*"
    status=1
}

# 0. Ambito del loop. El ritual de tres clases y ledger encadenado se gana el sueldo
#    donde un defecto cuesta partidas -el motor y el cerebro- y no donde cuesta una
#    errata. Los entregables fuera de `closing.deliverable_scope` se verifican con el
#    gate y sus venenos, sin ledger.
#    ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071
in_scope() {
    local file="$1" prefix
    while read -r prefix; do
        [[ -z "$prefix" ]] && continue
        [[ "$file" == "$prefix"* ]] && return 0
    done < <(jq -r '.closing.deliverable_scope[]? // empty' "$CONFIG")
    return 1
}

verify_ledger() {
    local slug="$1" file="$2" ledger="$3" dir="$4"

    # 1. Schema minimo y status CLOSED.
    local deliverable ledger_status iterations
    deliverable="$(jq -r '.deliverable // empty' "$ledger")"
    ledger_status="$(jq -r '.status // empty' "$ledger")"
    iterations="$(jq -r '.iterations | length' "$ledger")"
    [[ -n "$deliverable" ]] || fail "$slug: ledger sin campo deliverable"
    [[ "$deliverable" == "$file" ]] ||
        fail "$slug: el ledger apunta a '$deliverable' y STATE.md declara '$file'"
    if [[ "$ledger_status" == "BLOQUEADO" ]]; then
        fail "$slug: ledger BLOQUEADO ($(jq -r '.blocked_reason // "sin razon declarada"' "$ledger"))"
    else
        [[ "$ledger_status" == "CLOSED" ]] ||
            fail "$slug: status='$ledger_status', se esperaba CLOSED"
    fi

    # 8. thresholds_sha256 == sha256 del config/loop.json del arbol.
    #
    # Lo que esta regla persigue es relajar un umbral DESPUES de cerrar un loop, en
    # silencio, para que lo medido siga pareciendo valido. Un cambio de umbral aprobado y
    # con ADR no es eso, pero invalida igual el sha de todos los ledgers ya cerrados, que
    # no se pueden rejugar por algo que no les concierne.
    #
    # Por eso el ledger puede declarar enmiendas: cada una con el sha nuevo, el ADR que la
    # justifica y el motivo. Se acepta el sha del arbol si es el original O el de una
    # enmienda declarada CUYO ADR EXISTE en el arbol. Un cambio sin ADR sigue fallando, que
    # es exactamente lo que la regla queria impedir.
    local sha ok_sha
    sha="$(jq -r '.thresholds_sha256 // empty' "$ledger")"
    ok_sha=0
    [[ "$sha" == "$CONFIG_SHA" ]] && ok_sha=1
    if [[ $ok_sha -eq 0 ]]; then
        local n_enm i enm_sha enm_adr
        n_enm="$(jq -r '[.thresholds_amendments // []] | flatten | length' "$ledger")"
        for ((i = 0; i < n_enm; i++)); do
            enm_sha="$(jq -r ".thresholds_amendments[$i].sha256 // empty" "$ledger")"
            enm_adr="$(jq -r ".thresholds_amendments[$i].adr // empty" "$ledger")"
            [[ "$enm_sha" == "$CONFIG_SHA" ]] || continue
            if [[ -z "$enm_adr" || ! -f "$enm_adr" ]]; then
                fail "$slug: la enmienda de umbrales que casa cita el ADR '$enm_adr', que no existe"
                break
            fi
            echo "OK   $slug: umbrales enmendados por $enm_adr"
            ok_sha=1
            break
        done
    fi
    [[ $ok_sha -eq 1 ]] ||
        fail "$slug: thresholds_sha256 no coincide con config/loop.json ($sha vs $CONFIG_SHA) y no hay enmienda declarada con ADR"

    # 2. >=3 iteraciones VALIDAS, numeradas 1..N sin huecos, con >=3 clases distintas.
    #
    # Una iteracion marcada `annulled` queda en el ledger por trazabilidad -es evidencia
    # de trabajo real, y de su arreglo- pero no cuenta para el minimo ni para los
    # umbrales: es lo que la regla de la ronda ANULADA pide. Toda iteracion anulada tiene
    # que declarar `annulled_reason`.
    local annulled valid
    annulled="$(jq -r '[.iterations[] | select(.annulled == true)] | length' "$ledger")"
    valid=$((iterations - annulled))
    local missing_reason
    missing_reason="$(jq -r '[.iterations[] | select(.annulled == true)
        | select((.annulled_reason // "") == "")] | length' "$ledger")"
    [[ "$missing_reason" == "0" ]] || fail "$slug: hay iteraciones anuladas sin annulled_reason"

    if [[ "$valid" -lt "$MIN_ITER" ]]; then
        fail "$slug: $valid iteraciones validas (de $iterations), minimo $MIN_ITER"
        return
    fi
    if [[ "$iterations" -lt "$MIN_ITER" ]]; then
        fail "$slug: $iterations iteraciones, minimo $MIN_ITER"
        return
    fi
    if [[ "$iterations" -gt "$MAX_ITER" ]]; then
        fail "$slug: $iterations iteraciones, techo $MAX_ITER sin cierre (escalar a humano)"
    fi
    local numbering
    numbering="$(jq -r '[.iterations[].n] == [range(1; (.iterations|length)+1)]' "$ledger")"
    [[ "$numbering" == "true" ]] || fail "$slug: las iteraciones no van 1..N sin huecos"

    # Al menos 3 clases DISTINTAS en todo el ledger. No se exige que sean justo las tres
    # primeras: cuando una iteracion encuentra un hallazgo mayor, la regla del loop obliga
    # a repetir ESA clase sobre el commit del arreglo, asi que exigir las tres primeras
    # distintas seria incompatible con repetir. ver docs/decisions/ADR-0005-cierre-del-loop.md
    local distinct
    distinct="$(jq -r '[.iterations[] | select(.annulled != true) | .class] | unique | length' "$ledger")"
    [[ "$distinct" -ge 3 ]] || fail "$slug: solo $distinct clases distintas, minimo 3"

    local mandatory
    mandatory="$(jq -r --slurpfile cfg "$CONFIG" \
        '[$cfg[0].closing.mandatory_classes[]] - [.iterations[].class] | length' "$ledger")"
    [[ "$mandatory" == "0" ]] || fail "$slug: faltan clases obligatorias del loop"

    # 3. Encadenamiento, SHAs reales y started_at estrictamente crecientes.
    #
    # `commit_after(i)` tiene que ser ANCESTRO O IGUAL de `commit_before(i+1)`: eso es lo
    # que impide reordenar o inventar iteraciones, sin exigir que entre dos iteraciones no
    # haya pasado nada mas en el repositorio (lo que obligaria a congelar el arbol entero
    # mientras se cierra el loop de un entregable).
    # ver docs/decisions/ADR-0005-cierre-del-loop.md
    local chain_count
    chain_count="$(jq -r '.iterations | length' "$ledger")"
    for ((c = 0; c + 1 < chain_count; ++c)); do
        local after before
        after="$(jq -r ".iterations[$c].commit_after" "$ledger")"
        before="$(jq -r ".iterations[$((c + 1))].commit_before" "$ledger")"
        if ! git merge-base --is-ancestor "$after" "$before" 2>/dev/null; then
            fail "$slug: commit_after(i) != commit_before(i+1) (ni es ancestro suyo): $after -> $before"
        fi
    done

    local increasing
    increasing="$(jq -r '[.iterations[].started_at] as $t
        | [range(0; ($t|length)-1) | $t[.] < $t[.+1]] | all' "$ledger")"
    [[ "$increasing" == "true" ]] || fail "$slug: started_at no es estrictamente creciente"

    # "En la historia" quiere decir ALCANZABLE desde HEAD, no solo que el objeto exista:
    # un commit reescrito con --amend sigue resolviendo en la maquina donde se reescribio
    # y no existe en ningun clon. ver docs/decisions/ADR-0013-auditorias-fuera-del-loop.md#d-0123
    while read -r commit; do
        [[ -z "$commit" ]] && continue
        if ! git cat-file -e "${commit}^{commit}" 2>/dev/null; then
            fail "$slug: el commit $commit no existe en la historia"
        elif ! git merge-base --is-ancestor "$commit" HEAD 2>/dev/null; then
            fail "$slug: el commit $commit no es alcanzable desde HEAD (reescrito?)"
        fi
    done < <(jq -r '.iterations[] | .commit_before, .commit_after' "$ledger")

    # 4. exit_code, duration_ms >= min_duration_ms de la clase, y log con sha256 correcto.
    local n
    for ((n = 1; n <= iterations; ++n)); do
        local class duration min_duration log_sha exit_code log_path
        class="$(jq -r ".iterations[$((n - 1))].class" "$ledger")"
        duration="$(jq -r ".iterations[$((n - 1))].duration_ms // -1" "$ledger")"
        exit_code="$(jq -r ".iterations[$((n - 1))].exit_code // empty" "$ledger")"
        log_sha="$(jq -r ".iterations[$((n - 1))].log_sha256 // empty" "$ledger")"
        min_duration="$(jq -r ".classes[\"$class\"].min_duration_ms // empty" "$CONFIG")"

        local is_annulled
        is_annulled="$(jq -r ".iterations[$((n - 1))].annulled // false" "$ledger")"

        [[ -n "$exit_code" ]] || fail "$slug i$n: sin exit_code"
        [[ -n "$min_duration" ]] || {
            fail "$slug i$n: clase '$class' desconocida en $CONFIG"
            continue
        }
        if [[ "$is_annulled" != "true" && "$duration" -lt "$min_duration" ]]; then
            fail "$slug i$n: duration_ms=$duration < min_duration_ms=$min_duration ($class)"
        fi

        log_path="$dir/i${n}.log"
        if [[ ! -f "$log_path" ]]; then
            fail "$slug i$n: falta $log_path"
        else
            local real_sha
            real_sha="$(sha256sum "$log_path" | awk '{print $1}')"
            [[ "$real_sha" == "$log_sha" ]] ||
                fail "$slug i$n: log_sha256 no coincide con $log_path"
        fi

        # 7. checks_added no vacio en toda iteracion que produce commit.
        local added same_commit
        added="$(jq -r ".iterations[$((n - 1))].checks_added | length" "$ledger")"
        same_commit="$(jq -r ".iterations[$((n - 1))] | .commit_before == .commit_after" "$ledger")"
        if [[ "$added" == "0" && "$same_commit" == "false" ]]; then
            fail "$slug i$n: checks_added vacio en una iteracion que produjo commit"
        fi
        if [[ "$same_commit" == "true" ]]; then
            local commands
            commands="$(jq -r ".iterations[$((n - 1))].commands_run // [] | length" "$ledger")"
            [[ "$commands" != "0" ]] ||
                fail "$slug i$n: iteracion sin commit y sin commands_run (no es trabajo verificable)"
        fi

        # 7b. El agente declarado es el que config/loop.json asigna a la clase. Sin esto,
        #     un ledger puede decir que audito quien no audito.
        local declared_agent expected_agent
        declared_agent="$(jq -r ".iterations[$((n - 1))].agent // empty" "$ledger")"
        expected_agent="$(jq -r ".classes[\"$class\"].agent // empty" "$CONFIG")"
        # `main` (el hilo principal) vale para cualquier clase: hay entregables, como el
        # propio gate, donde no interviene ningun subagente especialista. Lo que el check
        # impide es lo contrario: declarar que audito un especialista que no audito.
        if [[ -n "$expected_agent" && "$declared_agent" != "$expected_agent" &&
              "$declared_agent" != "main" ]]; then
            fail "$slug i$n: agent='$declared_agent' pero la clase $class la audita '$expected_agent' (o 'main')"
        fi

        # 8. Umbrales de la clase sobre las metricas de la iteracion.
        #
        # Solo sobre la ULTIMA iteracion no anulada de cada clase. Una iteracion que
        # encuentra un incumplimiento tiene que registrarlo -es su trabajo-, y la regla
        # del loop obliga a repetir esa misma clase sobre el commit del arreglo. Exigir
        # el umbral tambien a la iteracion que lo descubrio haria que ningun ledger
        # pudiera cerrarse jamas despues de un hallazgo.
        # ver docs/decisions/ADR-0007-umbrales-por-clase.md#d-0060
        local is_last_of_class
        is_last_of_class="$(jq -r --arg class "$class" --argjson n "$n" \
            '[.iterations[] | select(.annulled != true) | select(.class == $class) | .n]
             | max == $n' "$ledger")"
        if [[ "$is_annulled" != "true" && "$is_last_of_class" == "true" ]]; then
            verify_metrics "$slug" "$n" "$class" "$ledger"
            verify_applicability "$slug" "$n" "$class" "$ledger"
        fi
    done

    # 5. Todo finding REPARADO tiene fixed_in y ese commit toca un archivo citado.
    local findings
    findings="$(jq -r '[.iterations[].findings[]?] | length' "$ledger")"
    for ((f = 0; f < findings; ++f)); do
        local estado fixed_in cite
        estado="$(jq -r "[.iterations[].findings[]?][$f].estado // empty" "$ledger")"
        fixed_in="$(jq -r "[.iterations[].findings[]?][$f].fixed_in // empty" "$ledger")"
        cite="$(jq -r "[.iterations[].findings[]?][$f].cite // empty" "$ledger")"
        [[ "$estado" == "REPARADO" ]] || continue
        if [[ -z "$fixed_in" ]]; then
            fail "$slug: finding REPARADO sin fixed_in"
            continue
        fi
        local touched cited_file
        touched="$(git show --name-only --format= "$fixed_in" 2>/dev/null)"
        cited_file="${cite%%:*}"
        if [[ -n "$cited_file" ]] && ! grep -qF "$cited_file" <<<"$touched"; then
            # Un ADR referenciado tambien vale como justificacion.
            if ! grep -q 'docs/decisions/' <<<"$cite"; then
                fail "$slug: $fixed_in no toca ningun archivo citado en '$cite'"
            fi
        fi
    done

    # 6. payload_sha256 no se repite entre iteraciones del mismo agente.
    local dup
    dup="$(jq -r '[.iterations[] | "\(.agent)|\(.payload_sha256)"] | length as $n
        | (unique | length) as $u | $n - $u' "$ledger")"
    [[ "$dup" == "0" ]] || fail "$slug: payload_sha256 repetido entre iteraciones del mismo agente"

    # 9. Las 2 ultimas iteraciones sin hallazgos y sobre el mismo commit_after;
    #    ratio DESCARTADO/total y ningun SIN_VERIFICAR abierto.
    local tail_clean tail_same
    tail_clean="$(jq -r --argjson k "$CLEAN_TAIL" \
        '[.iterations[-$k:][].findings | length] | all(. == 0)' "$ledger")"
    tail_same="$(jq -r --argjson k "$CLEAN_TAIL" \
        '[.iterations[-$k:][].commit_after] | unique | length == 1' "$ledger")"
    [[ "$tail_clean" == "true" ]] ||
        fail "$slug: las $CLEAN_TAIL ultimas iteraciones no estan limpias de hallazgos"
    [[ "$tail_same" == "true" ]] ||
        fail "$slug: las $CLEAN_TAIL ultimas iteraciones no comparten commit_after"

    local ratio_ok
    ratio_ok="$(jq -r --argjson max "$DISCARDED_MAX" \
        '[.iterations[].findings[]?] as $f
         | ($f | length) as $total
         | if $total == 0 then true
           else (([$f[] | select(.estado == "DESCARTADO")] | length) / $total) <= $max end' \
        "$ledger")"
    [[ "$ratio_ok" == "true" ]] || fail "$slug: ratio DESCARTADO/total por encima de $DISCARDED_MAX"

    local unverified
    unverified="$(jq -r '[.iterations[].findings[]? | select(.estado == "SIN_VERIFICAR")] | length' \
        "$ledger")"
    [[ "$unverified" == "0" ]] || fail "$slug: hay $unverified hallazgos SIN_VERIFICAR abiertos"

    # 9b. Auditorias del criterio 14. No son iteraciones -contarlas como tales hacia que
    #     "la auditoria encontro algo" significara "el loop no cierra"- pero SI son
    #     obligatorias, y sus hallazgos se cierran igual que los de una iteracion.
    #     ver docs/decisions/ADR-0013-auditorias-fuera-del-loop.md#d-0121
    local audits
    audits="$(jq -r '.auditorias // [] | length' "$ledger")"
    if [[ "$audits" == "0" ]]; then
        fail "$slug: sin bloque auditorias; el criterio 14 exige al menos una"
    fi

    local sin_campos
    sin_campos="$(jq -r '[.auditorias[]? | select(
            (.agent // "") == "" or (.commit_auditado // "") == ""
            or (.log_sha256 // "") == "" or (.findings | type) != "array")] | length' "$ledger")"
    [[ "$sin_campos" == "0" ]] ||
        fail "$slug: $sin_campos auditorias sin agent, commit_auditado, log_sha256 o findings"

    local a_total
    a_total="$(jq -r '.auditorias // [] | length' "$ledger")"
    for ((a = 0; a < a_total; ++a)); do
        local a_log a_sha a_real a_commit
        a_log="$(jq -r ".auditorias[$a].log" "$ledger")"
        a_sha="$(jq -r ".auditorias[$a].log_sha256" "$ledger")"
        a_commit="$(jq -r ".auditorias[$a].commit_auditado" "$ledger")"
        if [[ ! -f "$a_log" ]]; then
            fail "$slug: la auditoria $((a + 1)) declara un log inexistente ($a_log)"
            continue
        fi
        a_real="$(sha256sum "$a_log" | awk '{print $1}')"
        [[ "$a_real" == "$a_sha" ]] ||
            fail "$slug: sha256 de $a_log no coincide con el declarado"
        # ALCANZABLE desde HEAD, no solo existente: un commit reescrito con --amend sigue
        # resolviendo como objeto huerfano en la maquina donde se reescribio, y no existe
        # en ningun clon. Exigir solo `cat-file -e` dejaba pasar un ledger que apuntaba a
        # un commit que nadie mas iba a ver.
        if ! git cat-file -e "${a_commit}^{commit}" 2>/dev/null; then
            fail "$slug: la auditoria $((a + 1)) audito un commit inexistente ($a_commit)"
        elif ! git merge-base --is-ancestor "$a_commit" HEAD 2>/dev/null; then
            fail "$slug: la auditoria $((a + 1)) audito $a_commit, que no es alcanzable desde HEAD (reescrito?)"
        fi
    done

    # Ningun hallazgo de auditoria puede quedarse abierto: REPARADO con su commit, o
    # DESCARTADO con su razon. Nada mas.
    local a_abiertos
    a_abiertos="$(jq -r '[.auditorias[]?.findings[]?
        | select(.estado != "REPARADO" and .estado != "DESCARTADO")] | length' "$ledger")"
    [[ "$a_abiertos" == "0" ]] ||
        fail "$slug: $a_abiertos hallazgos de auditoria abiertos"

    local a_sin_commit
    a_sin_commit="$(jq -r '[.auditorias[]?.findings[]?
        | select(.estado == "REPARADO") | select((.fixed_in // "") == "")] | length' "$ledger")"
    [[ "$a_sin_commit" == "0" ]] ||
        fail "$slug: $a_sin_commit hallazgos de auditoria REPARADO sin fixed_in"

    local a_sin_razon
    a_sin_razon="$(jq -r '[.auditorias[]?.findings[]?
        | select(.estado == "DESCARTADO") | select((.razon // "") == "")] | length' "$ledger")"
    [[ "$a_sin_razon" == "0" ]] ||
        fail "$slug: $a_sin_razon hallazgos de auditoria DESCARTADO sin razon"

    # Mismo antifraude que en las iteraciones: el commit del arreglo tiene que tocar un
    # archivo citado por el propio hallazgo.
    local a_pares
    a_pares="$(jq -r '.auditorias[]?.findings[]? | select(.estado == "REPARADO")
        | "\(.fixed_in)|\(.cite)|\(.id)"' "$ledger")"
    while IFS='|' read -r fixed cite fid; do
        [[ -n "$fixed" ]] || continue
        local tocados
        tocados="$(git show --name-only --format= "$fixed" 2>/dev/null)"
        if [[ -z "$tocados" ]]; then
            fail "$slug: $fid dice arreglarse en $fixed, que no existe"
            continue
        fi
        grep -qF "${cite%%#*}" <<<"$tocados" ||
            fail "$slug: $fid ($cite) arreglado en $fixed, que no toca ese archivo"
    done <<<"$a_pares"

    # 10. Si i1-i3 cerraron sin hallazgos, hace falta la prueba de mutantes de i2.
    local first_three_clean
    first_three_clean="$(jq -r '[.iterations[0:3][].findings | length] | all(. == 0)' "$ledger")"
    if [[ "$first_three_clean" == "true" ]]; then
        local mutants killed min_mutants min_ratio
        mutants="$(jq -r '[.iterations[] | select(.class == "robustness") | .metrics.mutants // 0]
            | max // 0' "$ledger")"
        killed="$(jq -r '[.iterations[] | select(.class == "robustness")
            | .metrics.mutants_killed_ratio // 0] | max // 0' "$ledger")"
        min_mutants="$(jq -r '.classes.robustness.thresholds.mutants_min' "$CONFIG")"
        min_ratio="$(jq -r '.classes.robustness.thresholds.mutants_killed_ratio_min' "$CONFIG")"
        if [[ "$mutants" -lt "$min_mutants" ]]; then
            fail "$slug: i1-i3 limpias sin prueba de mutantes (mutants=$mutants < $min_mutants)"
        fi
        awk -v a="$killed" -v b="$min_ratio" 'BEGIN{exit !(a >= b)}' ||
            fail "$slug: ratio de mutantes muertos $killed < $min_ratio"
    fi
}

# Toda metrica exigida por la clase esta presente, o declarada inaplicable con motivo.
#
# Sin esto, un umbral se saltaba simplemente no reportando su metrica: `verify_metrics`
# solo compara cuando el valor existe. Era una puerta abierta en TODOS los entregables
# desde la fase 0, y ningun veneno la cubria.
# ver docs/decisions/ADR-0019-aplicabilidad-de-umbrales.md#d-0181
verify_applicability() {
    local slug="$1" n="$2" class="$3" ledger="$4"
    local idx=$((n - 1))
    local problemas
    problemas="$(jq -r --arg class "$class" --arg slug "$slug" --argjson i "$idx" \
        --slurpfile cfg "$CONFIG" '
        ($cfg[0].classes[$class].metricas_exigidas // []) as $exigidas
        | ($cfg[0].applicability[$slug][$class].no_aplica // {}) as $na
        | ($cfg[0].applicability[$slug][$class].metricas_extra // []) as $extra
        | (.iterations[$i].metrics // {}) as $m
        | [ ($exigidas + $extra)[]
            | select($m[.] == null)
            | if ($na[.] // "") == "" then "falta la metrica \(.) y no esta declarada inaplicable"
              else empty end ]
          + [ $na | to_entries[] | . as $e
              | select(($exigidas | index($e.key)) == null)
              | "se declara inaplicable \($e.key), que no es una metrica exigida de \($class)" ]
          + [ $na | to_entries[] | select(.value == "")
              | "\(.key) declarada inaplicable sin motivo" ]
        | join("; ")' "$ledger")"
    [[ -z "$problemas" ]] || fail "$slug i$n ($class): $problemas"
}

verify_metrics() {
    local slug="$1" n="$2" class="$3" ledger="$4"
    local idx=$((n - 1))
    local result
    result="$(jq -r --arg class "$class" --argjson i "$idx" --slurpfile cfg "$CONFIG" '
        ($cfg[0].classes[$class].thresholds) as $t
        | (.iterations[$i].metrics // {}) as $m
        | [
            if $t.diverge_max != null and $m.diverge != null and $m.diverge > $t.diverge_max
              then "diverge=\($m.diverge) > \($t.diverge_max)" else empty end,
            if $t.sin_verificar_max != null and $m.sin_verificar != null
               and $m.sin_verificar > $t.sin_verificar_max
              then "sin_verificar=\($m.sin_verificar)" else empty end,
            if $t.sanitizer_findings_max != null and $m.sanitizer_findings != null
               and $m.sanitizer_findings > $t.sanitizer_findings_max
              then "sanitizer_findings=\($m.sanitizer_findings)" else empty end,
            if $t.illegal_moves_max != null and $m.illegal_moves != null
               and $m.illegal_moves > $t.illegal_moves_max
              then "illegal_moves=\($m.illegal_moves)" else empty end,
            if $t.deadline_violations_max != null and $m.deadline_violations != null
               and $m.deadline_violations > $t.deadline_violations_max
              then "deadline_violations=\($m.deadline_violations)" else empty end,
            if $t.fuzz_states_min != null and $m.fuzz_states != null
               and $m.fuzz_states < $t.fuzz_states_min
              then "fuzz_states=\($m.fuzz_states) < \($t.fuzz_states_min)" else empty end,
            if $t.failsafe_levels_covered_min != null and $m.failsafe_levels_covered != null
               and $m.failsafe_levels_covered < $t.failsafe_levels_covered_min
              then "failsafe_levels_covered=\($m.failsafe_levels_covered)" else empty end,
            if $t.p99_move_ms_max != null and $m.p99_move_ms != null
               and $m.p99_move_ms > $t.p99_move_ms_max
              then "p99_move_ms=\($m.p99_move_ms) > \($t.p99_move_ms_max)" else empty end,
            if $t.max_move_ms_max != null and $m.max_move_ms != null
               and $m.max_move_ms > $t.max_move_ms_max
              then "max_move_ms=\($m.max_move_ms) > \($t.max_move_ms_max)" else empty end,
            if $t.timeouts_max != null and $m.timeouts != null and $m.timeouts > $t.timeouts_max
              then "timeouts=\($m.timeouts)" else empty end,
            if $t.hot_path_allocs_max != null and $m.hot_path_allocs != null
               and $m.hot_path_allocs > $t.hot_path_allocs_max
              then "hot_path_allocs=\($m.hot_path_allocs)" else empty end,
            if $t.broken_anchors_max != null and $m.broken_anchors != null
               and $m.broken_anchors > $t.broken_anchors_max
              then "broken_anchors=\($m.broken_anchors)" else empty end,
            if $t.size_bytes_mismatch_max != null and $m.size_bytes_mismatch != null
               and $m.size_bytes_mismatch > $t.size_bytes_mismatch_max
              then "size_bytes_mismatch=\($m.size_bytes_mismatch)" else empty end,
            if $t.frontmatter_invalid_max != null and $m.frontmatter_invalid != null
               and $m.frontmatter_invalid > $t.frontmatter_invalid_max
              then "frontmatter_invalid=\($m.frontmatter_invalid)" else empty end,
            if $t.duplicated_facts_max != null and $m.duplicated_facts != null
               and $m.duplicated_facts > $t.duplicated_facts_max
              then "duplicated_facts=\($m.duplicated_facts) > \($t.duplicated_facts_max)"
              else empty end,
            if $t.last_verified_max_age_days != null and $m.last_verified_max_age_days != null
               and $m.last_verified_max_age_days > $t.last_verified_max_age_days
              then "last_verified_max_age_days=\($m.last_verified_max_age_days)" else empty end
          ] | join("; ")' "$ledger")"
    [[ -z "$result" ]] || fail "$slug i$n ($class): umbral incumplido -> $result"
}

echo "== lint del loop, fase $PHASE =="
for row in "${DELIVERABLES[@]}"; do
    slug="$(awk -F'|' '{gsub(/ /,"",$2); print $2}' <<<"$row")"
    file="$(awk -F'|' '{gsub(/ /,"",$3); print $3}' <<<"$row")"
    [[ -z "$slug" ]] && continue

    if ! in_scope "$file"; then
        fail "$slug: '$file' esta fuera de closing.deliverable_scope y no lleva loop; quitalo del bloque loop-deliverables de STATE.md"
        continue
    fi

    ledger=".loop/${PHASE}/${slug}.ledger.json"
    dir=".loop/${PHASE}/${slug}"
    if [[ ! -f "$ledger" ]]; then
        fail "$slug: falta $ledger (entregable declarado en STATE.md)"
        continue
    fi
    if ! jq empty "$ledger" 2>/dev/null; then
        fail "$slug: $ledger no es JSON valido"
        continue
    fi
    verify_ledger "$slug" "$file" "$ledger" "$dir"
    [[ $status -eq 0 ]] && echo "OK   $slug ($(jq -r '.iterations | length' "$ledger") iteraciones)"
done

if [[ $status -ne 0 ]]; then
    echo "check 9 FAIL"
    exit 1
fi
echo "check 9 PASS: ${#DELIVERABLES[@]} entregables con ledger CLOSED"
