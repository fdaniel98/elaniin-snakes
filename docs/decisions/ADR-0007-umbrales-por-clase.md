---
title: "ADR-0007: los umbrales del loop se exigen a la ultima iteracion de cada clase"
read_when: "antes de tocar scripts/loop_verify.sh o de discutir por que un ledger no cierra"
authority: canonical
last_verified: 2026-09-15
size_bytes: 2966
---


## D-0060 Contexto {#d-0060}

Dos reglas del loop se contradicen, por cuarta vez y con la misma forma que las tres de
docs/decisions/ADR-0005-cierre-del-loop.md#d-0044:

1. Check 9, regla 8: las metricas de **cada** iteracion cumplen el umbral de su clase.
2. Regla de parada: una iteracion con un hallazgo `blocker` o `mayor` se arregla y se
   **repite esa misma clase** sobre el commit del arreglo.

Una iteracion que encuentra un incumplimiento tiene que registrarlo: esa medicion es su
producto. Pero queda en el ledger para siempre, asi que con la regla 1 literal ningun
entregable puede cerrar despues de un hallazgo numerico, ni arreglandolo. Es exactamente
lo que dejo bloqueado el ledger de `scripts/gate.sh`: la i6 midio
`duplicated_facts = 13`, la i7 midio 0 sobre el commit del arreglo, y el check 9 seguia
rojo por la i6.

## D-0061 Decision {#d-0061}

El umbral de una clase se exige a la **ultima iteracion no anulada de esa clase**. Las
anteriores conservan sus metricas como historia de lo que se encontro.

La garantia que protegia la regla original se mantiene entera: un ledger solo cierra si
cada clase **termina** dentro de umbral, y la cola limpia (dos iteraciones sin hallazgos
sobre el mismo `commit_after`) sigue exigiendo que esa ultima medicion sea posterior al
arreglo. Lo que desaparece es la imposibilidad, no la comprobacion.

Aprobado por el humano el 2026-09-15, enunciada la alternativa.

## D-0062 Alternativas descartadas {#d-0062}

| Alternativa | Por que no |
|---|---|
| Dejar la regla literal | El ledger del gate no puede cerrarse por ninguna via honesta; la fase se queda PARCIAL para siempre |
| Reescribir la metrica de la i6 | Falsea lo que aquella iteracion midio, que es justo lo que el antifraude existe para impedir |
| Marcar la i6 como `annulled` | `annulled` esta definido para rondas en las que fallo el gate (ver docs/decisions/ADR-0005-cierre-del-loop.md#d-0045). Usarlo para una iteracion que hizo su trabajo vaciaria el campo de significado |
| Exigir el umbral solo a la cola limpia | Una clase auditada a mitad del loop y nunca repetida se quedaria sin comprobar |

## D-0063 Consecuencias {#d-0063}

- `scripts/loop_verify.sh` calcula, por clase, el `n` maximo entre las iteraciones no
  anuladas, y solo ahi aplica `verify_metrics`.
- `gate-selftest.sh` gana `poison_9c`: una ultima iteracion de clase con una metrica fuera
  de umbral tiene que hacer fallar el check 9. Sin ese veneno, la regla nueva podria pasar
  por estar vacia.
- Una metrica fuera de umbral en una iteracion intermedia ya no rompe el gate, pero sigue
  siendo un hallazgo: si nadie lo repara, la clase no se repite, la cola no queda limpia
  y el ledger no cierra igualmente.
