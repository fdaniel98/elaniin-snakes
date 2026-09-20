---
title: "Pack: modificar el loop"
read_when: "antes de tocar config/loop.json, scripts/loop_verify.sh o un ledger de .loop/"
authority: derived
last_verified: 2026-09-20
size_bytes: 1833
---

## CP-50 Que cargar, en orden {#cp-50}

<!-- BEGIN:pack-load -->
| Orden | Archivo | Para que |
|---|---|---|
| 1 | `config/loop.json` | los umbrales y el ambito, que es lo que vas a tocar |
| 2 | `scripts/loop_verify.sh` | el check 9, que es quien los lee |
<!-- END:pack-load -->

Se separo de ver docs/context-packs/tocar-el-harness.md porque los dos juntos no caben en
el presupuesto por tarea (ver docs/INDEX.md#i-02): `loop_verify.sh` y `gate-selftest.sh`
son los dos archivos mas grandes del harness y rara vez se tocan a la vez.

## CP-51 Reglas duras {#cp-51}

1. `config/loop.json` exige aprobacion humana explicita y ADR, igual que el gate
   (regla de oro 10 de `CLAUDE.md`). **Endurecer** esta autorizado por la regla 4;
   aflojar un umbral, no.
2. Ningun umbral se escribe en `scripts/loop_verify.sh`: el ledger copia el sha256 del
   JSON y el gate lo compara. Relajar un umbral en el mismo commit que cierra un loop es
   fallo de fase.
3. Omitir una metrica exigida sin declararla en `applicability` con su motivo hace fallar
   el check 9 (ver docs/decisions/ADR-0019-aplicabilidad-de-umbrales.md#d-0181).
4. Un entregable nuevo en `deliverable_scope` deja el check 9 en rojo hasta que su ledger
   exista. Eso es correcto: quitarlo de `STATE.md` para ver verde es mentir
   (ver docs/decisions/ADR-0032-arena-en-el-ambito-del-loop.md#d-0322).

## CP-52 Criterio de salida {#cp-52}

- ADR que diga que cambio y por que, con la alternativa descartada.
- `./scripts/loop_verify.sh` en verde, o en rojo por un ledger que falta de verdad.
- El veneno correspondiente en `gate-selftest.sh` si el cambio añade comprobacion.
