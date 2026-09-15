---
title: ADR-0005: como se cierra el loop sin contradecir el antifraude
read_when: "antes de cambiar config/loop.json o scripts/loop_verify.sh"
authority: canonical
last_verified: 2026-09-15
size_bytes: 3402
---


## D-0040 Contexto {#d-0040}

La especificacion del loop pide tres cosas que, juntas, son imposibles de cumplir:

1. Cierre: las dos ultimas iteraciones deben compartir `commit_after`.
2. Encadenamiento: `commit_after(i_n) == commit_before(i_n+1)`.
3. Antifraude: `checks_added` no vacio **en cada** iteracion.

De 1 y 2 se deduce que la ultima iteracion no produce ningun commit; de 3, que tiene que
producirlo, porque un check ejecutable nuevo es un cambio en el arbol.

## D-0041 Decision {#d-0041}

`checks_added` se exige **en toda iteracion que produzca commit**, es decir, cuando
`commit_before != commit_after`. Una iteracion limpia final, que por definicion no cambia
el arbol, debe traer en su lugar `commands_run` no vacio con sus `exit_code`, ademas del
`i<N>.log` y su sha256.

Asi se conserva lo que el antifraude protege -que una iteracion limpia sea trabajo
verificable y no un `findings: []` escrito a mano- sin exigir un imposible.

## D-0042 Alternativas descartadas {#d-0042}

| Alternativa | Por que no |
|---|---|
| Relajar el cierre y no exigir mismo `commit_after` | Es la parte que impide declarar cerrado un loop cuyo ultimo arreglo nadie ha vuelto a verificar |
| Relajar el encadenamiento | Es lo unico que impide reordenar o inventar iteraciones a posteriori |
| Permitir `checks_added` vacio sin mas | Convierte la reparacion cosmetica en iteracion valida, que es justo lo que la regla evita |

## D-0044 Dos reglas mas que chocaban entre si {#d-0044}

La misma tension aparecio en otros dos sitios al cerrar la fase 0, y se resuelve con el
mismo criterio: conservar lo que la regla protege, sin exigir lo imposible.

| Regla original | Por que no se puede cumplir tal cual | Como queda |
|---|---|---|
| Las **tres primeras** iteraciones son de clases distintas | Un hallazgo mayor obliga a **repetir esa misma clase** sobre el commit del arreglo. Si la clase se repite en i2 e i3, las tres primeras ya no son distintas | Se exigen **3 clases distintas en todo el ledger**, contando solo iteraciones no anuladas |
| `commit_after(i)` es **igual** a `commit_before(i+1)` | Obligaria a congelar el repositorio entero mientras se cierra el loop de un entregable: cualquier commit de otro entregable rompe la igualdad | `commit_after(i)` tiene que ser **ancestro o igual** de `commit_before(i+1)`, que es lo que impide reordenar o inventar iteraciones |

## D-0045 Rondas anuladas {#d-0045}

La especificacion dice que una ronda en la que el gate falla queda **ANULADA** y no cuenta
para el minimo, pero no dice donde queda. Decision: se queda **en el ledger**, con
`annulled: true` y `annulled_reason` obligatorio, y el check 9 la excluye del minimo, de
las clases y de los umbrales.

Borrarla seria peor: una ronda anulada suele ser justo donde aparecio el defecto, y su
rastro es lo que permite auditar que el arreglo existio.

## D-0043 Estado {#d-0043}

**Pendiente de aprobacion humana explicita**, las tres decisiones de este ADR. Estan
escritas asi en `scripts/loop_verify.sh` para que la fase 0 pueda cerrarse, y anotadas en
`STATE.md` como decision abierta. Si el humano prefiere otra resolucion, se cambia el
script y este ADR queda como historia.
