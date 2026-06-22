#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int green_duration;
    int yellow_duration;
    int allred_duration;
    int ped_duration;
    int ped_max_wait;
    int emg_response_time;
    int vehicle_interval;
    int ped_interval;
    int emg_interval;
    int run_duration;
} Config;

int  load_config(const char *path, Config *cfg);
void print_config(const Config *cfg);

#endif
