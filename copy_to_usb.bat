@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
title Копирование Universal GPU Unlock на флешку

echo ====================================================
echo   Копирование Universal GPU Unlock на USB-флешку (FAT32)
echo ====================================================
echo.

set "DRIVE=%~1"

:: 1. Если буква диска не передана в аргументах, ищем флешку автоматически
if "!DRIVE!"=="" (
    set "AUTO_DRIVE="
    
    :: Сначала ищем накопитель, где уже лежит загрузчик BOOTX64.EFI или логи анлока
    for %%D in (D E F G H I J K L M N O P Q R S T U V W X Y Z) do (
        if not defined AUTO_DRIVE (
            if exist "%%D:\EFI\BOOT\BOOTX64.EFI" set "AUTO_DRIVE=%%D"
            if exist "%%D:\UNLOCK-LOGS" set "AUTO_DRIVE=%%D"
        )
    )
    
    :: Если не нашли по файлам, ищем любой подключенный съемный USB-диск (Removable)
    if not defined AUTO_DRIVE (
        for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "(Get-CimInstance Win32_LogicalDisk | Where-Object DriveType -eq 2 | Select-Object -ExpandProperty DeviceID -First 1)" 2^>nul`) do (
            set "AUTO_DRIVE=%%D"
        )
        if defined AUTO_DRIVE set "AUTO_DRIVE=!AUTO_DRIVE:~0,1!"
    )

    if defined AUTO_DRIVE (
        echo [OK] Обнаружена USB-флешка: !AUTO_DRIVE!:
        set /p "DRIVE=Нажмите Enter для записи на [!AUTO_DRIVE!:] или укажите другую букву: "
        if "!DRIVE!"=="" set "DRIVE=!AUTO_DRIVE!"
    ) else (
        set /p "DRIVE=Введите букву вашей USB-флешки (например E или F): "
    )
)

if "!DRIVE!"=="" (
    echo.
    echo [ОШИБКА] Буква диска не указана!
    echo.
    pause
    exit /b 1
)

set "DRIVE=!DRIVE:~0,1!:"

:: Защита от случайной перезаписи системного диска C:
if /i "!DRIVE!"=="C:" (
    echo.
    echo [ОШИБКА] Диск C: является системным диском Windows! Укажите букву USB-флешки.
    echo.
    pause
    exit /b 1
)

if not exist "!DRIVE!\" (
    echo.
    echo [ОШИБКА] Диск !DRIVE! не найден! Проверьте подключение флешки.
    echo.
    pause
    exit /b 1
)

echo.
echo Целевой накопитель: !DRIVE!\
echo Копирование файлов и прошивок на !DRIVE!\ ...

:: 1. Копируем загрузчик и структуру EFI
if exist "%~dp0dist\*" xcopy /E /I /Y "%~dp0dist\*" "!DRIVE!\" >nul
if not exist "!DRIVE!\EFI\ASSETS" mkdir "!DRIVE!\EFI\ASSETS" >nul 2>&1

:: 2. Копируем все ассеты прошивок (Ampere + Ada) в \EFI\ASSETS\
if exist "%~dp0UNIVERSAL-ASSETS\*" (
    echo Копирование микрокодов прошивок (*.bin)...
    xcopy /E /I /Y "%~dp0UNIVERSAL-ASSETS\*" "!DRIVE!\EFI\ASSETS\" >nul
)

if exist "%~dp0EFI\*" xcopy /E /I /Y "%~dp0EFI\*" "!DRIVE!\EFI\" >nul

:: 3. Вспомогательные утилиты для дампа VBIOS в Windows
if exist "%~dp0dump_vbios.bat" copy /Y "%~dp0dump_vbios.bat" "!DRIVE!\" >nul
if exist "%~dp0dump_vbios.ps1" copy /Y "%~dp0dump_vbios.ps1" "!DRIVE!\" >nul
if exist "%~dp0enable_laptop_hybrid.bat" copy /Y "%~dp0enable_laptop_hybrid.bat" "!DRIVE!\" >nul
if exist "%~dp0enable_laptop_hybrid.ps1" copy /Y "%~dp0enable_laptop_hybrid.ps1" "!DRIVE!\" >nul
if exist "%~dp0nvflashk.exe" (
    copy /Y "%~dp0nvflashk.exe" "!DRIVE!\" >nul
) else if exist "%~dp0..\nvflashk.exe" (
    copy /Y "%~dp0..\nvflashk.exe" "!DRIVE!\" >nul
)
if exist "%~dp0usb_release_staging\ИНСТРУКЦИЯ.txt" copy /Y "%~dp0usb_release_staging\ИНСТРУКЦИЯ.txt" "!DRIVE!\" >nul

:: 4. Проверка записи
if exist "!DRIVE!\EFI\ASSETS\gsp_ga10x.bin" (
    echo.
    echo ====================================================
    echo [УСПЕХ] Флешка !DRIVE!\ полностью готова к загрузке!
    echo         Загрузчик и все ассеты прошивок записаны.
    echo ====================================================
) else (
    echo.
    echo [ОШИБКА] Не удалось скопировать ассеты прошивки на !DRIVE!\.
    echo          Проверьте права записи и свободное место на диске.
)

echo.
if "%~1"=="" pause
