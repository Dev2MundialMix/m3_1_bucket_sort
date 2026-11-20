/*
 * info_maquina.c
 * Descobre CPU, núcleos, RAM, sistema operacional, compilador e versão do OpenMP.
 *
 * Cada sistema operacional expõe essas informações de um jeito diferente, por isso
 * o código é separado com #ifdef:
 *   - Windows: funções da API do Windows (windows.h) e o Registro do sistema;
 *   - Linux:   arquivos de texto em /proc e /etc, mantidos pelo próprio sistema.
 * O compilador define _WIN32 ao gerar um programa para Windows e __linux__ para
 * Linux, então a escolha é feita na compilação, sem custo durante a execução.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include "info_maquina.h"

// Remove espaços, aspas e quebras de linha do começo e do fim do texto
// (o nome da CPU, por exemplo, costuma vir com espaços sobrando).
static void apara(char *texto) {
    const char *lixo = " \t\r\n\"";
    char *inicio = texto;
    while (*inicio != '\0' && strchr(lixo, *inicio))
        inicio++;
    memmove(texto, inicio, strlen(inicio) + 1);

    int n = (int)strlen(texto);
    while (n > 0 && strchr(lixo, texto[n - 1]))
        texto[--n] = '\0';
}

#ifdef _OPENMP
// _OPENMP vale a data (aaaamm) da especificação OpenMP suportada pelo compilador.
static const char *versao_openmp(int data) {
    if (data >= 202111) return "5.2";
    if (data >= 202011) return "5.1";
    if (data >= 201811) return "5.0";
    if (data >= 201511) return "4.5";
    if (data >= 201307) return "4.0";
    if (data >= 201107) return "3.1";
    if (data >= 200805) return "3.0";
    return "2.5 ou anterior";
}
#endif


#if defined(_WIN32)
/* =============================== WINDOWS =============================== */
#include <windows.h>

// Lê um texto do Registro do Windows (em HKEY_LOCAL_MACHINE).
// Se a chave não existir, devolve "desconhecido".
static void le_registro(const char *chave, const char *valor, char *destino, DWORD tamanho) {
    DWORD tam = tamanho;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, chave, valor, RRF_RT_REG_SZ, NULL, destino, &tam) != ERROR_SUCCESS)
        snprintf(destino, tamanho, "desconhecido");
}

static void coleta_dependente_do_sistema(InfoMaquina *info) {
    // --- Sistema operacional ---
    const char *chave_versao = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    char produto[96], versao[32], build[32];
    le_registro(chave_versao, "ProductName", produto, sizeof produto);
    le_registro(chave_versao, "DisplayVersion", versao, sizeof versao);
    le_registro(chave_versao, "CurrentBuild", build, sizeof build);

    // O Windows 11 continua gravando "Windows 10" no ProductName (por compatibilidade).
    // O jeito confiável de diferenciar é o número do build: 22000 em diante é Windows 11.
    if (atoi(build) >= 22000 && strncmp(produto, "Windows 10", 10) == 0)
        produto[9] = '1';   // "Windows 10 ..." -> "Windows 11 ..."
    snprintf(info->sistema, sizeof info->sistema, "%s %s (build %s)", produto, versao, build);

    // --- Processador ---
    le_registro("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString",
                info->cpu, sizeof info->cpu);
    apara(info->cpu);

    // --- Núcleos físicos ---
    // A função devolve uma lista de "relações" (núcleos, caches, soquetes...).
    // Cada item do tipo RelationProcessorCore é um núcleo físico.
    // A 1ª chamada (com NULL) só informa quantos bytes a lista precisa.
    DWORD tamanho = 0;
    GetLogicalProcessorInformation(NULL, &tamanho);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *itens = malloc(tamanho);
    if (itens != NULL && GetLogicalProcessorInformation(itens, &tamanho)) {
        int qtd = tamanho / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        for (int i = 0; i < qtd; i++)
            if (itens[i].Relationship == RelationProcessorCore)
                info->nucleos_fisicos++;
    }
    free(itens);

    // --- Memória RAM ---
    MEMORYSTATUSEX memoria;
    memoria.dwLength = sizeof memoria;   // a API exige que o tamanho seja preenchido antes
    if (GlobalMemoryStatusEx(&memoria))
        info->ram_gb = memoria.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);

    // --- Nome do computador ---
    DWORD tam_host = sizeof info->host;
    if (!GetComputerNameA(info->host, &tam_host))
        snprintf(info->host, sizeof info->host, "maquina");
}


#elif defined(__linux__)
/* ================================ LINUX ================================ */
#include <unistd.h>
#include <sys/utsname.h>

// Procura num arquivo de texto a primeira linha que começa com "campo" e copia
// o valor que vem depois de ':' ou '='. Serve para os três arquivos usados:
//   /proc/cpuinfo   ->  model name : AMD Ryzen 5 7600 6-Core Processor
//   /proc/meminfo   ->  MemTotal:       32562444 kB
//   /etc/os-release ->  PRETTY_NAME="Ubuntu 24.04 LTS"
static int le_campo(const char *arquivo, const char *campo, char *destino, int tamanho) {
    char linha[512];
    int achou = 0;
    FILE *f = fopen(arquivo, "r");
    if (f == NULL)
        return 0;
    while (!achou && fgets(linha, sizeof linha, f)) {
        char *separador = strpbrk(linha, ":=");
        if (strncmp(linha, campo, strlen(campo)) == 0 && separador != NULL) {
            snprintf(destino, tamanho, "%s", separador + 1);
            apara(destino);
            achou = 1;
        }
    }
    fclose(f);
    return achou;
}

static void coleta_dependente_do_sistema(InfoMaquina *info) {
    char texto[64];

    // --- Sistema operacional ---
    char distro[128] = "Linux";
    struct utsname kernel;
    le_campo("/etc/os-release", "PRETTY_NAME", distro, sizeof distro);
    uname(&kernel);
    // No WSL (Linux rodando dentro do Windows) a versão do kernel contém "microsoft".
    // Vale anotar, pois o desempenho pode ser diferente de um Linux instalado direto.
    const char *wsl = (strstr(kernel.release, "microsoft") || strstr(kernel.release, "Microsoft"))
                      ? " [WSL]" : "";
    snprintf(info->sistema, sizeof info->sistema, "%s (kernel %s)%s", distro, kernel.release, wsl);

    // --- Processador e núcleos físicos ---
    le_campo("/proc/cpuinfo", "model name", info->cpu, sizeof info->cpu);
    // "cpu cores" = núcleos físicos de um processador (considera máquina com 1 soquete).
    if (le_campo("/proc/cpuinfo", "cpu cores", texto, sizeof texto))
        info->nucleos_fisicos = atoi(texto);

    // --- Memória RAM (o valor vem em kB) ---
    if (le_campo("/proc/meminfo", "MemTotal", texto, sizeof texto))
        info->ram_gb = atof(texto) / (1024.0 * 1024.0);

    // --- Nome do computador ---
    if (gethostname(info->host, sizeof info->host) != 0)
        snprintf(info->host, sizeof info->host, "maquina");
    info->host[sizeof info->host - 1] = '\0';
}


#else
/* ========================== OUTROS SISTEMAS ========================== */
static void coleta_dependente_do_sistema(InfoMaquina *info) {
    snprintf(info->sistema, sizeof info->sistema, "nao suportado (apenas Windows e Linux)");
}
#endif


// Preenche a estrutura com tudo o que conseguir descobrir sobre a máquina.
void coleta_info_maquina(InfoMaquina *info) {
    memset(info, 0, sizeof *info);
    snprintf(info->sistema, sizeof info->sistema, "desconhecido");
    snprintf(info->host, sizeof info->host, "maquina");
    snprintf(info->cpu, sizeof info->cpu, "desconhecido");
    snprintf(info->compilador, sizeof info->compilador, "desconhecido");
    snprintf(info->openmp, sizeof info->openmp, "nao habilitado (compile com -fopenmp)");

    coleta_dependente_do_sistema(info);

    // Núcleos lógicos: o próprio OpenMP informa quantos processadores enxerga,
    // do mesmo jeito nos dois sistemas.
    info->nucleos_logicos = omp_get_num_procs();
    if (info->nucleos_fisicos <= 0)
        info->nucleos_fisicos = info->nucleos_logicos;

#if defined(__GNUC__) && !defined(__clang__)
    snprintf(info->compilador, sizeof info->compilador, "GCC %s", __VERSION__);
#endif
#ifdef _OPENMP
    snprintf(info->openmp, sizeof info->openmp, "%s (_OPENMP = %d)", versao_openmp(_OPENMP), _OPENMP);
#endif
}
