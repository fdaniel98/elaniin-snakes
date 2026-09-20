---
title: De donde salen las reglas, y por que el motor es codigo propio
read_when: "antes de publicar el repositorio, de elegirle licencia o de copiar algo de una fuente ajena"
authority: derived
source: github.com/BattlesnakeOfficial/rules (AGPL-3.0), verificado 2026-09-20
last_verified: 2026-09-20
size_bytes: 3861
---

# ADR-0033 — De donde salen las reglas {#adr-0033}

## D-0330 Contexto {#d-0330}

Antes del primer push se audito el repositorio. De la auditoria salio un dato que no
estaba escrito en ningun sitio y que se habia dado por supuesto: **`BattlesnakeOfficial/rules`
se publica bajo AGPL-3.0**, no bajo una licencia permisiva. Es la fuente de la que sale
`docs/rules.md` entero, y con el todo `engine/src/rules.cpp`.

Importa porque la AGPL es copyleft fuerte: si `engine/` fuera obra derivada de ese codigo,
publicarlo como "codigo propio" sin licencia seria incompatible con ella.

## D-0331 Decision {#d-0331}

De esa fuente se tomaron **las reglas del juego, no su codigo**, y el repositorio se
publica como codigo propio.

Lo que sostiene la distincion, comprobable en el arbol:

- **No hay ni un fichero `.go`** en el repositorio, ni un bloque de Go pegado en un
  comentario o en la documentacion. El unico codigo ajeno es `cpp-httplib`, MIT, con su
  `LICENSE` al lado y declarado en ver docs/SOURCES.md#s-03.
- Las citas `archivo.go:linea` de `docs/rules.md` son **referencias a donde comprobar un
  hecho**, no fragmentos transcritos. Lo que se escribio en castellano es el
  comportamiento observable: en que orden se resuelven las fases, cuando se libera la
  cola, que pasa con un empate a la cabeza.
- Ese comportamiento es lo que cualquier snake tiene que cumplir para jugar. Una serpiente
  que no reprodujera el orden de fases del arbitro no seria original: seria incorrecta.
- Las estructuras propias no se parecen a las suyas y no podrian: bitboards parametrizados
  en tiempo de compilacion, estado trivialmente copiable en ring buffers, `Rng` propio
  porque el suyo no se puede reproducir sin copiar una tabla de 607 constantes
  (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0092).

Lo que **no** es esto: una sala limpia. Nadie escribio una especificacion de espaldas al
codigo para que otro la implementara sin verlo. La fuente se leyo directamente. La postura
es que reglas de un juego son procedimientos y no expresion protegible, y que de ahi no se
copio expresion alguna.

Decidido por el humano el 2026-09-20, sobre el dato de la licencia puesto encima de la
mesa. **No es asesoria legal**, y queda escrito aqui para que la decision tenga fecha,
motivo y alcance en vez de ser un supuesto.

## D-0332 Lo que esta decision obliga {#d-0332}

1. Sigue prohibido copiar codigo de una snake ajena, con o sin licencia permisiva
   (ver zoo/README.md). El zoo clona a un temporal **fuera del repositorio** y solo
   construye imagenes.
2. Toda fuente nueva se anota en ver docs/SOURCES.md#s-03 **con su licencia**. Que la de
   `rules` faltara durante seis fases es el fallo que este ADR corrige.
3. Si algun dia hiciera falta un fragmento literal de una fuente copyleft, no se copia: se
   para y se decide de nuevo.

## D-0333 Alternativas descartadas {#d-0333}

| Alternativa | Por que no |
|---|---|
| Publicar el repositorio bajo AGPL-3.0 por precaucion | Asume que hay obra derivada donde no la hay, y arrastra a copyleft trabajo que es propio |
| Dejarlo privado | Evita la pregunta en vez de responderla |
| Reescribir `docs/rules.md` quitando las citas `archivo.go:linea` | Las citas son lo que hace verificable cada regla (regla de oro 1). Quitarlas empeora el repositorio para aparentar distancia |

## D-0334 Estado {#d-0334}

**ACEPTADA.** Queda abierto, y es otra decision, que licencia lleva el repositorio: hoy no
hay fichero `LICENSE`, lo que por defecto significa todos los derechos reservados.
