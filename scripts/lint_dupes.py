#!/usr/bin/env python3
"""Detector determinista de hechos duplicados entre documentos.

Un hecho duplicado es un pasaje que dos archivos distintos afirman con las mismas
palabras en el mismo orden, en vez de que uno lo afirme y el otro lo enlace por
anchor (ver docs/INDEX.md#i-04).

El criterio es mecanico a proposito: n-gramas de palabras normalizadas. No opina
sobre el sentido; mide repeticion literal, que es lo que se puede comprobar en un
gate. Salida: una linea por pasaje duplicado y, al final, el conteo.

    ./scripts/lint_dupes.py            -> informe legible, sale 0
    ./scripts/lint_dupes.py --json     -> {"duplicated_facts": N, "pairs": [...]}
    ./scripts/lint_dupes.py --max N    -> sale 1 si el conteo supera N
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import unicodedata
from pathlib import Path

# Tamano del n-grama. Doce palabras seguidas identicas en dos archivos no es
# coincidencia del idioma: es el mismo hecho escrito dos veces.
NGRAM = 12

# Que se mira. docs/results/ lo genera el Training Room y docs/decisions/ registra
# decisiones, que por definicion citan el contexto que las motivo.
EXCLUDE_PREFIXES = ("docs/results/", "docs/decisions/", "PROMPT-MAESTRO")

CODE_FENCE = re.compile(r"^\s*```")
FRONT_MATTER = re.compile(r"^---\s*$")
TABLE_ROW = re.compile(r"^\s*\|")
MD_LINK = re.compile(r"\[([^\]]*)\]\([^)]*\)")
ANCHOR_DEF = re.compile(r"\{#[a-z0-9-]+\}")
ANCHOR_CITE = re.compile(r"(?:ver|see)\s+\S+\.md#[a-z0-9-]+")
INLINE_CODE = re.compile(r"`[^`]*`")
NON_WORD = re.compile(r"[^a-z0-9 ]+")


def normalize(text: str) -> list[str]:
    """Texto markdown -> lista de palabras comparables."""
    text = ANCHOR_CITE.sub(" ", text)
    text = ANCHOR_DEF.sub(" ", text)
    text = MD_LINK.sub(r"\1", text)
    text = INLINE_CODE.sub(" ", text)
    text = unicodedata.normalize("NFKD", text)
    text = "".join(c for c in text if not unicodedata.combining(c))
    text = text.lower()
    text = NON_WORD.sub(" ", text)
    return text.split()


def prose_of(path: Path) -> list[str]:
    """Solo prosa: sin front-matter, sin bloques de codigo, sin filas de tabla."""
    out: list[str] = []
    in_fence = False
    in_front = False
    for i, line in enumerate(path.read_text(encoding="utf-8").splitlines()):
        if i == 0 and FRONT_MATTER.match(line):
            in_front = True
            continue
        if in_front:
            if FRONT_MATTER.match(line):
                in_front = False
            continue
        if CODE_FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence or TABLE_ROW.match(line):
            continue
        out.append(line)
    return out


def tracked_markdown(root: Path) -> list[str]:
    files = subprocess.run(
        ["git", "ls-files", "*.md"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.split()
    return sorted(f for f in files if not f.startswith(EXCLUDE_PREFIXES))


def ngrams(words: list[str]) -> dict[tuple[str, ...], int]:
    """n-grama -> indice de su primera aparicion."""
    out: dict[tuple[str, ...], int] = {}
    for i in range(len(words) - NGRAM + 1):
        out.setdefault(tuple(words[i : i + NGRAM]), i)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--max", type=int, default=None)
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent
    files = tracked_markdown(root)

    words: dict[str, list[str]] = {}
    grams: dict[str, dict[tuple[str, ...], int]] = {}
    for f in files:
        words[f] = normalize("\n".join(prose_of(root / f)))
        grams[f] = ngrams(words[f])

    pairs = []
    for a_i, a in enumerate(files):
        for b in files[a_i + 1 :]:
            shared = sorted(
                (grams[a][g], grams[b][g], g) for g in grams[a].keys() & grams[b].keys()
            )
            if not shared:
                continue
            # n-gramas solapados del mismo pasaje = un unico hecho duplicado.
            passages = []
            last_a = last_b = -99
            for ia, ib, g in shared:
                if ia - last_a <= NGRAM and ib - last_b <= NGRAM:
                    passages[-1]["words"] += 1
                else:
                    passages.append({"at_a": ia, "at_b": ib, "words": NGRAM, "text": " ".join(g)})
                last_a, last_b = ia, ib
            for p in passages:
                pairs.append({"a": a, "b": b, **p})

    count = len(pairs)
    if args.json:
        print(json.dumps({"duplicated_facts": count, "pairs": pairs}, ensure_ascii=False, indent=1))
    else:
        for p in pairs:
            print(f"DUP {p['a']} <-> {p['b']} ({p['words']} palabras)")
            print(f"    \"{p['text']}...\"")
        print(f"hechos duplicados: {count} (n-grama de {NGRAM} palabras, {len(files)} archivos)")

    if args.max is not None and count > args.max:
        print(f"FAIL {count} hechos duplicados > umbral {args.max}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
