# zoo/ - [FASE 3] snakes publicas en contenedores aislados

Se usan los **manifests TOML** de `BattlesnakeOfficial/snake-zoo` como catalogo, pero
**no** su runner: `src/docker.rs` lanza los contenedores con
`docker run -d --name ... -p 0:<puerto>`, sin `--user`, sin `--read-only`, sin
`--cap-drop`, y publicando en todas las interfaces.

## Aislamiento obligatorio

Todo contenedor del zoo se lanza con:

```
--user 65534:65534 --read-only --tmpfs /tmp --cap-drop=ALL
--security-opt=no-new-privileges --pids-limit=256
-p 127.0.0.1:<puerto host>:<puerto snake>
```

Nunca `-v`, nunca `/var/run/docker.sock`.

`docker build` ejecuta codigo arbitrario de terceros: se clona siempre a un directorio
temporal **fuera del repo**, `zoo add <repo-url>` de un repo nuevo exige confirmacion
humana una vez, y el manifest fija el commit SHA aprobado.

## Recursos justos

Todas las snakes, **la nuestra incluida**, corren con identicos `--cpus`, `--memory` y
`--cpuset-cpus` con pinning explicito, sin compartir nucleos entre contenedores ni con el
arbitro: `--cpus` se implementa con cuota CFS sobre un periodo de 100 ms, asi que sin
pinning el p99 lo domina el throttling, no el algoritmo. El orquestador **aborta** si la
suma de cuotas supera los nucleos fisicos menos dos.

Solo snakes con codigo publico; se respetan sus licencias. Sparring local si; copiar su
codigo, no.

Estado: no implementado. Lo abre la fase 3.
