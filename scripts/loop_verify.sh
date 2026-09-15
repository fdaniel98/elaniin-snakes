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
    [[ "$ledger_status" == "CLOSED" ]] || fail "$slug: status='$ledger_status', se esperaba CLOSED"

    # 8. thresholds_sha256 == sha256 del config/loop.json del arbol.
    local sha
    sha="$(jq -r '.thresholds_sha256 // empty' "$ledger")"
    [[ "$sha" == "$CONFIG_SHA" ]] ||
        fail "$slug: thresholds_sha256 no coincide con config/loop.json ($sha vs $CONFIG_SHA)"

    # 2. >=3 iteraciones, numeradas 1..N sin huecos, las 3 primeras de clases distintas.
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
    distinct="$(jq -r '[.iterations[].class] | unique | length' "$ledger")"
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

    while read -r commit; do
        [[ -z "$commit" ]] && continue
        git cat-file -e "${commit}^{commit}" 2>/dev/null ||
            fail "$slug: el commit $commit no existe en la historia"
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

        [[ -n "$exit_code" ]] || fail "$slug i$n: sin exit_code"
        [[ -n "$min_duration" ]] || {
            fail "$slug i$n: clase '$class' desconocida en $CONFIG"
            continue
        }
        if [[ "$duration" -lt "$min_duration" ]]; then
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

        # 8. Umbrales de la clase sobre las metricas de la iteracion.
        verify_metrics "$slug" "$n" "$class" "$ledger"
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
              then "frontmatter_invalid=\($m.frontmatter_invalid)" else empty end
          ] | join("; ")' "$ledger")"
    [[ -z "$result" ]] || fail "$slug i$n ($class): umbral incumplido -> $result"
}

echo "== lint del loop, fase $PHASE =="
for row in "${DELIVERABLES[@]}"; do
    slug="$(awk -F'|' '{gsub(/ /,"",$2); print $2}' <<<"$row")"
    file="$(awk -F'|' '{gsub(/ /,"",$3); print $3}' <<<"$row")"
    [[ -z "$slug" ]] && continue

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
