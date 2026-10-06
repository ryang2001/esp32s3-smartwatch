@echo off
set MSYSTEM=
set MSYS=
set MINGW64_ROOT=

set IDF_PATH=D:\esp\v6.0\esp-idf
set IDF_TOOLS_PATH=C:\Espressif\tools
set IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v6.0\venv
set ESP_ROM_ELF_DIR=C:\Espressif\tools\esp-rom-elfs\20241011

set IDF_PATH_ESCAPED=D:\esp\v6.0\esp-idf

set "PATH=C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;C:\Espressif\tools\cmake\4.0.3\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\idf-exe\1.0.3;C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\xtensa-esp-elf\bin;C:\Espressif\tools\python\v6.0\venv\Scripts;C:\Espressif\tools\python\v6.0\venv;C:\WINDOWS\system32;C:\WINDOWS;C:\Program Files\Git\cmd"

cd /d D:\esp\projects\blink
if exist build\CMakeCache.txt del /q build\CMakeCache.txt

cmake -B build -G Ninja -DIDF_PATH=%IDF_PATH% -DCMAKE_TOOLCHAIN_FILE=%IDF_PATH%\tools\cmake\toolchain-esp32s3.cmake
if errorlevel 1 (
    echo CMAKE FAILED
    exit /b 1
)

ninja -C build -j8
