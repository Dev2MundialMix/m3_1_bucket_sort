@echo off
REM Compila e executa no Windows. Precisa do gcc do MSYS2 (UCRT64) no PATH: veja o README.
REM Argumento opcional: tamanho do vetor. Exemplo: executar.bat 10000000
cd /d "%~dp0"
gcc -O2 -Wall -fopenmp bucket_sort.c info_maquina.c -o bucket_sort.exe
if errorlevel 1 (
    echo.
    echo Erro na compilacao. Veja a mensagem acima e a secao "Problemas comuns" do README.
    pause
    exit /b 1
)
bucket_sort.exe %*
REM O pause evita que a janela feche sozinha quando o arquivo e aberto com duplo clique.
pause
