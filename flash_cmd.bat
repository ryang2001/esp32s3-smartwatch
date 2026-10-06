@echo off
set "MSYSTEM="
set "IDF_PATH=D:\esp\v6.0\esp-idf"
set "IDF_TOOLS_PATH=C:\Espressif\tools"
set "IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v6.0\venv"
set "ESP_IDF_VERSION=6.0.0"
set "PATH=C:\Espressif\tools\python\v6.0\venv\Scripts;C:\Espressif\tools\cmake\4.0.3\bin;C:\Espressif\tools\ninja\1.12.1;%PATH%"
cd /d D:\esp\projects\blink
python "%IDF_PATH%\tools\idf.py" -p COM7 %*
