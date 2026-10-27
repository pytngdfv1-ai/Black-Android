@echo off
setlocal
rem Black para Windows con el menu de trucos. Necesita: Git, Visual Studio 2022 (C++), y tu ISO de Black (USA).
cd /d "%~dp0"
set "BLACK_COMMIT=d8dcd0f905b2fa26480ee91d05155209fa426005"
set "SDK_COMMIT=91146ff6e2a218b19c1109dc665d06020472ab57"

if not exist XboxXBE-Recomp (
    git clone https://github.com/TGP482/XboxXBE-Recomp.git || goto :fail
)
git -C XboxXBE-Recomp checkout %SDK_COMMIT% || goto :fail

if not exist BLACK-XBERecomp (
    git clone https://github.com/TGP482/BLACK-XBERecomp.git || goto :fail
    git -C BLACK-XBERecomp checkout %BLACK_COMMIT% || goto :fail
    git -C BLACK-XBERecomp apply --whitespace=nowarn "%~dp0cheats-windows.patch" || goto :fail
)

if not exist BLACK-XBERecomp\assets mkdir BLACK-XBERecomp\assets
dir /b BLACK-XBERecomp\assets\*.iso >nul 2>nul || (
    echo.
    echo   Copia tu ISO de Black en: %~dp0BLACK-XBERecomp\assets
    echo   y vuelve a ejecutar este archivo.
    pause
    exit /b 1
)

cd BLACK-XBERecomp
call build.bat
exit /b %errorlevel%

:fail
echo.
echo   Fallo. Revisa el mensaje de arriba.
pause
exit /b 1
