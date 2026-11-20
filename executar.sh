#!/bin/sh
# Compila e executa no Linux (também funciona no terminal "MSYS2 UCRT64" do Windows).
# Argumento opcional: tamanho do vetor. Exemplo: sh executar.sh 10000000
cd "$(dirname "$0")" || exit 1
gcc -O2 -Wall -fopenmp bucket_sort.c info_maquina.c -o bucket_sort || exit 1
./bucket_sort "$@"
