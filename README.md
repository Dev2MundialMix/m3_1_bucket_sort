# Bucket Sort com OpenMP: Avaliação 05 (Programação Paralela)

Este programa compara o Bucket Sort **sequencial** com a versão **paralela em OpenMP**. Ao terminar, ele grava um relatório com os tempos, o speedup e a configuração da máquina, que pode ser **Windows ou Linux**.

## Arquivos

| Arquivo | O que tem |
|---|---|
| `bucket_sort.c` | O algoritmo (versão sequencial e paralela), a medição de tempo e o relatório |
| `info_maquina.c` / `.h` | A coleta de CPU, núcleos, RAM, sistema, compilador e versão do OpenMP |
| `executar.bat` | Compila e executa no Windows |
| `executar.sh` | Compila e executa no Linux (e no terminal MSYS2) |

## 1. Instalar o compilador (gcc com OpenMP)

O projeto usa só o **gcc**, nos dois sistemas. O suporte a OpenMP é a biblioteca `libgomp`: no Linux ela já vem com o gcc, mas no MSYS2 (Windows) é um pacote separado.

### Windows (MSYS2)

1. Instale o MSYS2: `winget install MSYS2.MSYS2`, ou baixe o instalador em <https://www.msys2.org>.
2. No menu Iniciar, abra o terminal **MSYS2 UCRT64** e instale o gcc **e o OpenMP**:
   ```
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libgomp
   ```
3. Coloque o gcc no PATH, para poder usar o `executar.bat` no PowerShell ou no cmd:
   menu Iniciar → *Editar as variáveis de ambiente do sistema* → *Variáveis de Ambiente* → em **Path** (do usuário), clique em *Editar* → *Novo* → `C:\msys64\ucrt64\bin`.
   Depois feche e abra o terminal de novo. Para conferir, rode `gcc --version`.

### Linux (Ubuntu/Debian)

```
sudo apt update
sudo apt install gcc
```

## 2. Executar

Na pasta do projeto:

| Onde | Comando |
|---|---|
| Windows (PowerShell ou cmd) | `.\executar.bat` |
| Windows (terminal MSYS2 UCRT64) | `sh executar.sh` |
| Linux | `sh executar.sh` |

Sem argumentos, o programa roda a **bateria completa**: N = 1, 10 e 50 milhões de elementos, com 1, 2, ..., até o número de núcleos lógicos de threads. Isso pode levar alguns minutos.

Para testar **um só tamanho** (bom para a demonstração em aula):

```
.\executar.bat 10000000        (Windows)
sh executar.sh 10000000        (Linux)
```

Para compilar à mão:

```
gcc -O2 -Wall -fopenmp bucket_sort.c info_maquina.c -o bucket_sort
```

## 3. O relatório

A cada execução é criado o arquivo `relatorio_<nome-da-maquina>_<data>_<hora>.txt` na pasta do projeto. Ele traz o mesmo conteúdo que aparece na tela. Como o nome inclui a máquina e a hora, cada integrante do grupo pode rodar no próprio computador sem sobrescrever o relatório de ninguém.

Formato (os valores abaixo são só ilustrativos):

```
===== BUCKET SORT: SEQUENCIAL x PARALELO (OpenMP) =====
Data/hora   : 20/11/2026 15:40:12
Sistema     : Windows 10 Pro 22H2 (build 19045)
Maquina     : MEU-PC
CPU         : AMD Ryzen 5 7600 6-Core Processor
Nucleos     : 6 fisicos / 12 logicos
RAM         : 31.1 GB
Compilador  : GCC 14.2.0
OpenMP      : 4.5 (_OPENMP = 201511)
Parametros  : ~100 elementos por balde, media de 5 execucoes (+1 de aquecimento), semente 42

           N  Versao      Threads    Tempo(s)   Speedup  Eficiencia  Correto
    10000000  sequencial        -      x.xxxx      1.00           -      sim
    10000000  paralela          1      x.xxxx      x.xx       xx.x%      sim
    10000000  paralela          2      x.xxxx      x.xx       xx.x%      sim
    ...
```

- **Tempo(s)**: média de 5 execuções. Antes delas há 1 execução de aquecimento, que é descartada.
- **Speedup**: tempo sequencial ÷ tempo paralelo, ou seja, quantas vezes ficou mais rápido.
- **Eficiência**: speedup ÷ número de threads. O ideal seria 100%.
- **Correto**: na linha sequencial, confere se o vetor saiu ordenado. Nas linhas paralelas, confere se o resultado é idêntico ao da sequencial.

No Linux, o relatório mostra a distribuição e o kernel e marca `[WSL]` quando roda dentro do Windows.

## 4. Como o código está organizado

O algoritmo segue o livro de Cormen et al., *Algoritmos: Teoria e Prática*, seção 8.4. Os valores são reais uniformes em [0, 1), e cada valor vai para o balde `floor(valor * k)`. Depois cada balde é ordenado com insertion sort e os baldes são concatenados em ordem.

**Versão sequencial**: segue os 3 passos do livro: distribuir, ordenar cada balde, concatenar.

**Versão paralela**: tem 3 fases.

| Fase | O que faz | OpenMP |
|---|---|---|
| 1. Distribuição | Cada thread distribui a sua parte da entrada nos **seus próprios baldes**, então não há disputa nem trava. | `#pragma omp parallel` + `#pragma omp for schedule(static)` |
| 2. Posições | Soma os tamanhos para saber onde cada balde começa na saída. A fase é sequencial e barata. | nenhum |
| 3. Ordenação | Cada thread pega baldes inteiros, junta os pedaços na posição final e ordena com insertion sort. | `#pragma omp parallel for schedule(dynamic, 64)` |

Os comentários no código explicam o **porquê** de cada decisão.

## 5. Dicas para medir

- Feche programas pesados (navegador, jogos) antes de rodar.
- Em notebook, deixe **na tomada** e no modo de energia **Melhor desempenho**.
- Rode duas vezes e veja se os números ficam parecidos.
- **Memória:** a bateria padrão usa até ~2,5 GB de RAM. O tamanho máximo aceito é 100 milhões de elementos (~5 GB), pensado para máquinas de até 16 GB.

## 6. Problemas comuns

| Problema | Solução |
|---|---|
| `'gcc' não é reconhecido como um comando...` | O gcc não está no PATH. Refaça o passo 3 do Windows e abra um terminal novo. |
| `cannot find -lgomp` | Falta o OpenMP. No terminal MSYS2 UCRT64, rode `pacman -S mingw-w64-ucrt-x86_64-libgomp`. |
| Erro sobre `libgomp-1.dll` ao abrir o `.exe` | É o mesmo caso: `C:\msys64\ucrt64\bin` precisa estar no PATH. Outra saída é rodar pelo terminal MSYS2. |
| No Linux, erro com `$'\r'` ao rodar o `.sh` | O arquivo ficou com quebra de linha do Windows. Corrija com `sed -i 's/\r$//' executar.sh` |
