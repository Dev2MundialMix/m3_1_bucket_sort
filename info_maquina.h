/*
 * info_maquina.h
 * Coleta informações de hardware e software da máquina para o relatório.
 * A implementação muda conforme o sistema (Windows ou Linux): veja info_maquina.c.
 */
#ifndef INFO_MAQUINA_H
#define INFO_MAQUINA_H

typedef struct {
    char sistema[256];    // nome e versão do sistema operacional
    char host[64];        // nome do computador (vai no nome do arquivo de relatório)
    char cpu[128];        // modelo do processador
    int nucleos_fisicos;
    int nucleos_logicos;  // threads de hardware (com SMT/Hyper-Threading = 2x os físicos)
    double ram_gb;
    char compilador[64];
    char openmp[64];
} InfoMaquina;

void coleta_info_maquina(InfoMaquina *info);

#endif
