# Black para Windows + menu de trucos

Base: port de Windows de TGP482 (`BLACK-XBERecomp`) con el menu de trucos de la version Android.

1. Instala Git y Visual Studio 2022 con "Desktop development with C++".
2. Ejecuta `build_windows.bat` (clona el codigo, aplica `cheats-windows.patch` y compila).
3. Pon tu ISO de Black (USA) en `BLACK-XBERecomp\assets` cuando lo pida.
4. Abre el juego y pulsa **F10**: pestana **CHEATS** (municion, granadas, rapid fire, sin retroceso, god mode,
   one-hit kill, ESP, aimbot, velocidad, desbloquear misiones y Black Ops, y dar cualquier arma).

Sin probar en una PC: el parche compila a nivel de sintaxis, pero no se ha ejecutado el build completo.

## Compilar en GitHub Actions (sin instalar nada)

1. Cifra el `default.xbe` de tu ISO: `openssl enc -aes-256-cbc -pbkdf2 -iter 200000 -in default.xbe -out default.xbe.enc -pass pass:TU_CLAVE`
2. Sube `default.xbe.enc` a la carpeta `windows/` de la rama `windows-port`.
3. En Settings > Secrets and variables > Actions crea el secreto `XBE_KEY` con `TU_CLAVE`.
4. En la pestana Actions ejecuta **Windows** (Run workflow). Al terminar, descarga `Black-Windows` (zip) de los Artifacts.
