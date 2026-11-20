/*
 * bucket_sort.c
 * Avaliação 05 - Programação Paralela (UNIVALI) - Tema G: OpenMP - Bucket Sort
 *
 * Compara o Bucket Sort sequencial (referência) com a versão paralela em OpenMP
 * e grava um relatório com os tempos, o speedup e a configuração da máquina.
 *
 * Compilar:  gcc -O2 -Wall -fopenmp bucket_sort.c info_maquina.c -o bucket_sort
 * Executar:  ./bucket_sort            -> bateria completa (vários tamanhos e nº de threads)
 *            ./bucket_sort 10000000   -> apenas um tamanho (bom para a demonstração)
 *
 * Algoritmo (Cormen et al., "Algoritmos: Teoria e Prática", seção 8.4):
 *   os valores são números reais uniformes em [0, 1). O intervalo é dividido em k
 *   baldes de mesmo tamanho e cada valor vai para o balde floor(valor * k). Como o
 *   balde b só tem valores menores que os do balde b+1, basta ordenar cada balde
 *   (com insertion sort) e juntar os baldes em ordem.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <omp.h>
#include "info_maquina.h"
#ifdef _WIN32
#include <windows.h>   // só para o relógio de alta precisão (veja a função agora)
#endif

// O Cormen usa n baldes (1 elemento por balde, em média). Aqui usamos n/100 baldes:
//  - menos baldes = menos alocações de memória (cada balde é um vetor alocado);
//  - cada balde tem trabalho suficiente para valer a pena entregar a uma thread;
//  - com ~100 elementos o insertion sort continua rápido.
#define MEDIA_POR_BALDE 100

// Cada tempo é a média de várias execuções, para diminuir a influência de
// outros programas rodando na máquina.
#define REPETICOES 5

// Semente fixa: os dados gerados são sempre os mesmos, em qualquer máquina.
#define SEMENTE 42

// Limite pensando em máquinas de até 16 GB de RAM. O programa usa ~50 bytes por
// elemento (entrada + 2 vetores de saída + baldes), então 100 milhões de elementos
// ocupam ~5 GB, deixando folga para o sistema. A bateria padrão vai até 50 milhões (~2,5 GB).
#define N_MAXIMO 100000000


/* ============================ BALDE E AUXILIARES ============================ */

// Um balde é um vetor que cresce conforme recebe elementos.
// (O Cormen usa listas ligadas; o vetor faz o mesmo papel e simplifica o insertion sort.)
typedef struct {
    double *itens;
    int qtd;   // quantos elementos o balde tem
    int cap;   // quantos cabem antes de precisar realocar
} Balde;

// Insere um valor no fim do balde. Quando enche, a capacidade dobra:
// assim o realloc é chamado poucas vezes, e não a cada inserção.
static void adiciona(Balde *balde, double valor) {
    if (balde->qtd == balde->cap) {
        balde->cap = (balde->cap == 0) ? 8 : balde->cap * 2;
        balde->itens = realloc(balde->itens, balde->cap * sizeof(double));
        if (balde->itens == NULL) {
            printf("Memoria insuficiente.\n");
            exit(1);
        }
    }
    balde->itens[balde->qtd++] = valor;
}

// Insertion sort (Cormen, seção 2.1). É O(m^2), mas muito rápido para vetores
// pequenos como os baldes, e é o algoritmo indicado pelo livro para o Bucket Sort.
static void insertion_sort(double *v, int m) {
    for (int j = 1; j < m; j++) {
        double chave = v[j];
        int i = j - 1;
        while (i >= 0 && v[i] > chave) {   // empurra os maiores uma posição para a direita
            v[i + 1] = v[i];
            i--;
        }
        v[i + 1] = chave;
    }
}

static int numero_de_baldes(int n) {
    int k = n / MEDIA_POR_BALDE;
    return (k > 0) ? k : 1;
}


/* ===================== VERSÃO SEQUENCIAL (referência) ===================== */

// Os 3 passos do livro: distribuir, ordenar cada balde, concatenar.
void bucket_sort_sequencial(const double *entrada, double *saida, int n) {
    int k = numero_de_baldes(n);
    Balde *baldes = calloc(k, sizeof(Balde));   // calloc zera: todos começam vazios

    // 1) Distribuição: valor em [0, 1) vezes k dá um índice de 0 a k-1.
    for (int i = 0; i < n; i++) {
        int b = (int)(entrada[i] * k);
        adiciona(&baldes[b], entrada[i]);
    }

    // 2) Ordena cada balde.
    for (int b = 0; b < k; b++)
        insertion_sort(baldes[b].itens, baldes[b].qtd);

    // 3) Concatena os baldes, em ordem, no vetor de saída.
    int pos = 0;
    for (int b = 0; b < k; b++) {
        for (int j = 0; j < baldes[b].qtd; j++)
            saida[pos++] = baldes[b].itens[j];
        free(baldes[b].itens);
    }
    free(baldes);
}


/* ========================= VERSÃO PARALELA (OpenMP) ========================
 * Mesmos passos, com um cuidado: duas threads nunca podem escrever no mesmo
 * balde ao mesmo tempo (condição de corrida: uma inserção poderia apagar a outra).
 *
 * Fase 1 - Distribuição em paralelo: o vetor de entrada é dividido entre as
 *   threads e cada thread tem o SEU conjunto de k baldes. Assim ninguém disputa
 *   balde e não é preciso trava. (Outra opção seria um omp_lock por balde, mas
 *   cada inserção pagaria o custo de travar e destravar.)
 *
 * Fase 2 - Posições (sequencial): agora o balde b está dividido em pedaços, um
 *   por thread. Somando os tamanhos dos baldes anteriores, descobrimos onde o
 *   balde b começa no vetor de saída. Isso permite que, na fase 3, cada thread
 *   escreva direto no lugar certo sem esperar as outras.
 *
 * Fase 3 - Ordenação em paralelo: cada thread pega baldes inteiros, copia os
 *   pedaços do balde para a posição final e ordena ali mesmo. Baldes diferentes
 *   ocupam trechos diferentes da saída, então também não há condição de corrida.
 */
void bucket_sort_paralelo(const double *entrada, double *saida, int n) {
    int k = numero_de_baldes(n);
    int p = omp_get_max_threads();

    // baldes[t * k + b] = pedaço do balde b que pertence à thread t
    Balde *baldes = calloc((size_t)p * k, sizeof(Balde));
    int *tamanho = calloc(k, sizeof(int));   // tamanho total de cada balde
    int *inicio = malloc(k * sizeof(int));   // onde cada balde começa na saída

    // ---- FASE 1: distribuição (paralela) ----
    #pragma omp parallel
    {
        Balde *meus_baldes = &baldes[omp_get_thread_num() * k];

        // schedule(static): cada thread recebe um bloco contínuo do vetor de entrada.
        #pragma omp for schedule(static)
        for (int i = 0; i < n; i++) {
            int b = (int)(entrada[i] * k);
            adiciona(&meus_baldes[b], entrada[i]);
        }
    }   // barreira implícita: só segue quando todas as threads terminaram

    // ---- FASE 2: posição de cada balde na saída (sequencial) ----
    // Custo O(k * p), pequeno perto do resto. Percorre thread por thread porque
    // os baldes de uma mesma thread estão lado a lado na memória (melhor para a cache).
    for (int t = 0; t < p; t++)
        for (int b = 0; b < k; b++)
            tamanho[b] += baldes[t * k + b].qtd;

    int pos = 0;
    for (int b = 0; b < k; b++) {
        inicio[b] = pos;
        pos += tamanho[b];
    }

    // ---- FASE 3: junta os pedaços e ordena cada balde (paralela) ----
    // schedule(dynamic, 64): os baldes podem ter tamanhos diferentes. No modo
    // dinâmico, cada thread pega o próximo pacote de 64 baldes assim que termina
    // o anterior, e nenhuma fica parada esperando as outras.
    #pragma omp parallel for schedule(dynamic, 64)
    for (int b = 0; b < k; b++) {
        int destino = inicio[b];
        for (int t = 0; t < p; t++) {
            Balde *pedaco = &baldes[t * k + b];
            for (int j = 0; j < pedaco->qtd; j++)
                saida[destino++] = pedaco->itens[j];
            free(pedaco->itens);
        }
        insertion_sort(&saida[inicio[b]], tamanho[b]);
    }

    free(baldes);
    free(tamanho);
    free(inicio);
}


/* ============================ MEDIÇÃO E RELATÓRIO ============================ */

// Gera n números reais uniformes em [0, 1) com o gerador xorshift64 (Marsaglia, 2003).
// Não usamos rand() porque ele muda de um sistema para outro (no Windows só vai até
// 32767). Com um gerador próprio, os dados são idênticos no Windows e no Linux.
static void gera_dados(double *v, int n) {
    unsigned long long x = SEMENTE;
    for (int i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        v[i] = (x >> 11) * (1.0 / 9007199254740992.0);   // 53 bits aleatórios / 2^53
    }
}

static int esta_ordenado(const double *v, int n) {
    for (int i = 1; i < n; i++)
        if (v[i - 1] > v[i])
            return 0;
    return 1;
}

// Tempo de relógio atual, em segundos.
// No Linux, omp_get_wtime() é preciso. No Windows, o gcc (MinGW) implementa
// omp_get_wtime() com resolução de só 1 milissegundo, pouco para os testes rápidos;
// por isso lá usamos o contador de alta precisão do próprio Windows.
// (clock() não serve: no Linux ele soma o tempo de CPU de todas as threads.)
static double agora(void) {
#ifdef _WIN32
    LARGE_INTEGER frequencia, contador;
    QueryPerformanceFrequency(&frequencia);
    QueryPerformanceCounter(&contador);
    return (double)contador.QuadPart / (double)frequencia.QuadPart;
#else
    return omp_get_wtime();
#endif
}

typedef void (*FuncaoOrdenacao)(const double *entrada, double *saida, int n);

// Roda o algoritmo 1 vez para "aquecer" e depois REPETICOES vezes; devolve o tempo médio.
// O aquecimento é descartado porque a 1ª execução paga custos que não são do algoritmo:
// a criação das threads do OpenMP e o sistema entregando as páginas de memória.
static double mede_tempo(FuncaoOrdenacao ordena, const double *entrada, double *saida, int n) {
    ordena(entrada, saida, n);
    double inicio = agora();
    for (int r = 0; r < REPETICOES; r++)
        ordena(entrada, saida, n);
    return (agora() - inicio) / REPETICOES;
}

// Escreve o mesmo texto na tela e no arquivo de relatório.
// O fflush garante que o arquivo fica salvo mesmo se o programa for interrompido.
static void escreve(FILE *arquivo, const char *formato, ...) {
    va_list args;
    va_start(args, formato);
    vprintf(formato, args);
    va_end(args);
    va_start(args, formato);
    vfprintf(arquivo, formato, args);
    va_end(args);
    fflush(stdout);
    fflush(arquivo);
}

int main(int argc, char *argv[]) {
    int tamanhos[] = {1000000, 10000000, 50000000};
    int qtd_tamanhos = 3;

    // Com um argumento, testa só esse tamanho (útil na demonstração em aula).
    if (argc > 1) {
        double pedido = atof(argv[1]);
        if (pedido < 1 || pedido > N_MAXIMO) {
            printf("Tamanho invalido. Use um valor entre 1 e %d.\n", N_MAXIMO);
            return 1;
        }
        tamanhos[0] = (int)pedido;
        qtd_tamanhos = 1;
    }

    InfoMaquina info;
    coleta_info_maquina(&info);

    // O nome do arquivo leva o nome da máquina e a data/hora: cada integrante do
    // grupo pode rodar no seu computador sem sobrescrever o relatório de ninguém.
    time_t agora = time(NULL);
    char data_hora[32], carimbo[32], nome_arquivo[160];
    strftime(data_hora, sizeof data_hora, "%d/%m/%Y %H:%M:%S", localtime(&agora));
    strftime(carimbo, sizeof carimbo, "%Y%m%d_%H%M%S", localtime(&agora));
    snprintf(nome_arquivo, sizeof nome_arquivo, "relatorio_%s_%s.txt", info.host, carimbo);

    FILE *arquivo = fopen(nome_arquivo, "w");
    if (arquivo == NULL) {
        printf("Nao foi possivel criar o arquivo %s\n", nome_arquivo);
        return 1;
    }

    // Os textos de saída ficam sem acento para aparecerem certos no terminal do Windows.
    escreve(arquivo, "===== BUCKET SORT: SEQUENCIAL x PARALELO (OpenMP) =====\n");
    escreve(arquivo, "Data/hora   : %s\n", data_hora);
    escreve(arquivo, "Sistema     : %s\n", info.sistema);
    escreve(arquivo, "Maquina     : %s\n", info.host);
    escreve(arquivo, "CPU         : %s\n", info.cpu);
    escreve(arquivo, "Nucleos     : %d fisicos / %d logicos\n", info.nucleos_fisicos, info.nucleos_logicos);
    escreve(arquivo, "RAM         : %.1f GB\n", info.ram_gb);
    escreve(arquivo, "Compilador  : %s\n", info.compilador);
    escreve(arquivo, "OpenMP      : %s\n", info.openmp);
    escreve(arquivo, "Parametros  : ~%d elementos por balde, media de %d execucoes (+1 de aquecimento), semente %d\n\n",
            MEDIA_POR_BALDE, REPETICOES, SEMENTE);

    escreve(arquivo, "%12s  %-10s  %7s  %10s  %8s  %10s  %7s\n",
            "N", "Versao", "Threads", "Tempo(s)", "Speedup", "Eficiencia", "Correto");

    int max_threads = omp_get_num_procs();

    for (int i = 0; i < qtd_tamanhos; i++) {
        int n = tamanhos[i];
        double *entrada = malloc(n * sizeof(double));
        double *referencia = malloc(n * sizeof(double));   // saída da versão sequencial
        double *saida = malloc(n * sizeof(double));        // saída da versão paralela
        if (entrada == NULL || referencia == NULL || saida == NULL) {
            escreve(arquivo, "Memoria insuficiente para N = %d\n", n);
            free(entrada);
            free(referencia);
            free(saida);
            break;
        }
        gera_dados(entrada, n);

        // Sequencial: é a referência de tempo (speedup = 1) e de resultado.
        double t_seq = mede_tempo(bucket_sort_sequencial, entrada, referencia, n);
        int correto = esta_ordenado(referencia, n);
        escreve(arquivo, "%12d  %-10s  %7s  %10.4f  %8.2f  %10s  %7s\n",
                n, "sequencial", "-", t_seq, 1.0, "-", correto ? "sim" : "NAO");

        // Paralela com 1, 2, ..., max_threads threads.
        for (int t = 1; t <= max_threads; t++) {
            omp_set_num_threads(t);
            double t_par = mede_tempo(bucket_sort_paralelo, entrada, saida, n);

            // Correta se o resultado for idêntico, valor a valor, ao da versão sequencial.
            correto = memcmp(saida, referencia, n * sizeof(double)) == 0;

            double speedup = t_seq / t_par;           // quantas vezes mais rápido
            double eficiencia = 100.0 * speedup / t;  // quanto de cada thread foi aproveitado
            escreve(arquivo, "%12d  %-10s  %7d  %10.4f  %8.2f  %9.1f%%  %7s\n",
                    n, "paralela", t, t_par, speedup, eficiencia, correto ? "sim" : "NAO");
        }
        escreve(arquivo, "\n");

        free(entrada);
        free(referencia);
        free(saida);
    }

    fclose(arquivo);
    printf("Relatorio salvo em: %s\n", nome_arquivo);
    return 0;
}
