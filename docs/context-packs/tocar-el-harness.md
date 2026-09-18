---
title: "Pack: modificar el harness"
read_when: "antes de tocar scripts/, .claude/ o config/loop.json"
authority: derived
last_verified: 2026-09-15
size_bytes: 1859
---


## CP-40 Que cargar, en orden {#cp-40}

<!-- BEGIN:pack-load -->
| Orden | Archivo | Para que |
|---|---|---|
| 1 | `docs/harness.md` | que hace cada pieza y como se apaga |
| 2 | `scripts/gate.sh` | el oraculo que vas a tocar |
| 3 | `scripts/gate-selftest.sh` | el veneno que prueba el check |
| 4 | `config/loop.json` | los umbrales, si tocas el loop |
<!-- END:pack-load -->

`scripts/loop_verify.sh` -el check 9- se abre cuando toques el loop y no antes: es el
archivo mas grande del harness y no cabe en el presupuesto de la tarea
(ver docs/INDEX.md#i-02). Igual que `config/loop.json`, cambiarlo exige aprobacion humana
y ADR, asi que abrirlo no es el primer paso de nada.

## CP-41 Reglas duras {#cp-41}

1. El gate **solo crece** (regla de oro 4 de `CLAUDE.md`): un fallo que el gate no ve se
   arregla añadiendo el check, no relajandolo.
2. Toda modificacion que **reduzca** lo comprobado exige aprobacion humana explicita y un
   ADR. Comentar un check, `|| true`, reducir fixtures o relajar `clang-tidy` cuentan como
   reducir.
3. Un check nuevo necesita su veneno en `gate-selftest.sh` el mismo dia. Un check que pasa
   por estar vacio se ve igual que uno que funciona.
4. `config/loop.json` y `.claude/settings.json` tienen la misma proteccion que el gate.

## CP-42 Criterio de salida {#cp-42}

- `./scripts/gate-selftest.sh` en verde, incluido el veneno nuevo.
- `docs/harness.md` actualizado con la pieza y como desactivarla.
- ADR si la decision tiene alternativa descartada.
- Sin ledger: el harness esta fuera del ambito del loop
  (ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071). Lo que lo verifica es el gate
  y su veneno nuevo, no tres iteraciones.
