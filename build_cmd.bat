@echo off
set "MSYSTEM="
set "IDF_PATH=D:\esp\v6.0\esp-idf"
set "IDF_TOOLS_PATH=C:\Espressif\tools"
set "IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v6.0\venv"
set "ESP_IDF_VERSION=6.0.0"
set "PATH=C:\Espressif\tools\python\v6.0\venv\Scripts;C:\Espressif\tools\cmake\4.0.3\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;C:\Espressif\tools\idf-exe\1.0.3;C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;C:\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin;%PATH%"
cd /d D:\esp\projects\blink
python "%IDF_PATH%\tools\idf.py" build
