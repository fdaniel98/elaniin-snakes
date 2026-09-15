---
title: ADR-0005: como se cierra el loop sin contradecir el antifraude
read_when: "antes de cambiar config/loop.json o scripts/loop_verify.sh"
authority: derived
last_verified: 2026-09-15
size_bytes: 1936
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

## D-0043 Estado {#d-0043}

**Pendiente de aprobacion humana explicita.** Esta escrito asi en
`scripts/loop_verify.sh` para que la fase 0 pueda cerrarse, y esta anotado en `STATE.md`
como decision abierta. Si el humano prefiere otra resolucion, se cambia el script y este
ADR queda como historia.
