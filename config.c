#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

static void set_defaults(Config *c) {
    c->green_duration    = 10;
    c->yellow_duration   = 3;
    c->allred_duration   = 1;
    c->ped_duration      = 8;
    c->ped_max_wait      = 30;
    c->emg_response_time = 3;
    c->vehicle_interval  = 4;
    c->ped_interval      = 20;
    c->emg_interval      = 60;
    c->run_duration      = 0;
}

int load_config(const char *path, Config *cfg) {
    set_defaults(cfg);
    FILE *f = fopen(path, "r");
    if (!f) { perror("load_config"); return -1; }
    char line[256], key[64], val[64];
    while (fgets(line, sizeof line, f)) {
        if (line[0]=='#'||line[0]=='\n') continue;
        int ok=(sscanf(line," %63[^= \t] = %63s",key,val)==2)||
               (sscanf(line," %63[^=]=%63s",key,val)==2);
        if (!ok) continue;
        if      (!strcmp(key,"green_duration"))    cfg->green_duration    =atoi(val);
        else if (!strcmp(key,"yellow_duration"))   cfg->yellow_duration   =atoi(val);
        else if (!strcmp(key,"allred_duration"))   cfg->allred_duration   =atoi(val);
        else if (!strcmp(key,"ped_duration"))      cfg->ped_duration      =atoi(val);
        else if (!strcmp(key,"ped_max_wait"))      cfg->ped_max_wait      =atoi(val);
        else if (!strcmp(key,"emg_response_time")) cfg->emg_response_time =atoi(val);
        else if (!strcmp(key,"vehicle_interval"))  cfg->vehicle_interval  =atoi(val);
        else if (!strcmp(key,"ped_interval"))      cfg->ped_interval      =atoi(val);
        else if (!strcmp(key,"emg_interval"))      cfg->emg_interval      =atoi(val);
        else if (!strcmp(key,"run_duration"))      cfg->run_duration      =atoi(val);
    }
    fclose(f);
    return 0;
}

void print_config(const Config *cfg) {
    printf("┌──────────────────────────────────┐\n");
    printf("│  Traffic Light Configuration     │\n");
    printf("├──────────────────────────────────┤\n");
    printf("│  green duration     : %-6d sec  │\n", cfg->green_duration);
    printf("│  yellow duration    : %-6d sec  │\n", cfg->yellow_duration);
    printf("│  all-red gap        : %-6d sec  │\n", cfg->allred_duration);
    printf("│  pedestrian cross   : %-6d sec  │\n", cfg->ped_duration);
    printf("│  ped max wait       : %-6d sec  │\n", cfg->ped_max_wait);
    printf("│  emergency response : %-6d sec  │\n", cfg->emg_response_time);
    printf("│  vehicle interval   : %-6d sec  │\n", cfg->vehicle_interval);
    printf("│  run duration       : %-6d sec  │\n", cfg->run_duration);
    printf("└──────────────────────────────────┘\n\n");
}
