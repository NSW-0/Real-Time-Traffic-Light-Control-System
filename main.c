#include "traffic.h"
#include "shared_state.h"
#include "config.h"
#include "control.h"
#include "light.h"
#include "vehicle.h"
#include "pedestrian.h"
#include "emergency.h"
#include "logger.h"
#include "display.h"

static int g_mqid=-1, g_shmid=-1, g_semid=-1;

static void cleanup(void) {
    if (g_mqid  >= 0) { msgctl(g_mqid,  IPC_RMID, NULL); g_mqid  = -1; }
    if (g_semid >= 0) { sem_destroy(g_semid);             g_semid = -1; }
    if (g_shmid >= 0) { shm_destroy(g_shmid);             g_shmid = -1; }
}

static void sig_handler(int sig) {
    (void)sig;
    if (g_shmid >= 0) {
        SharedState *s = shm_attach(g_shmid);
        if (s) { s->running = 0; shm_detach(s); }
    }
}

static void broadcast_shutdown(int mqid) {
    TrafficMsg m;
    memset(&m, 0, sizeof m);
    m.cmd = CMD_SHUTDOWN;
    snprintf(m.info, sizeof m.info, "System shutdown");

    long targets[] = {
        MTYPE_TO_CONTROL,
        MTYPE_TO_LIGHT_N, MTYPE_TO_LIGHT_S,
        MTYPE_TO_LIGHT_E, MTYPE_TO_LIGHT_W,
        MTYPE_TO_LOGGER
    };
    for (size_t i = 0; i < sizeof targets / sizeof *targets; i++) {
        m.mtype = targets[i];
        mq_send(mqid, &m);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }

    Config cfg;
    if (load_config(argv[1], &cfg) < 0) return 1;
    print_config(&cfg);

    g_shmid = shm_create(); if (g_shmid < 0) return 1;
    g_semid = sem_create(); if (g_semid < 0) { cleanup(); return 1; }

    int old_mq = msgget(IPC_KEY_MSGQ, 0666);
    if (old_mq >= 0) msgctl(old_mq, IPC_RMID, NULL);
    g_mqid = msgget(IPC_KEY_MSGQ, IPC_CREAT | IPC_EXCL | 0666);
    if (g_mqid < 0) { perror("msgget"); cleanup(); return 1; }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("\n═══════════════════════════════════════\n");
    printf("   REAL-TIME TRAFFIC LIGHT SYSTEM\n");
    printf("═══════════════════════════════════════\n\n");

    pid_t pids[12];
    int   np = 0;

    /* 1. Logger (fork first so everyone can log from start) */
    pids[np] = fork();
    if (pids[np] == 0) { run_logger(g_mqid); exit(0); }
    np++;
    usleep(100000);

    /* 2. Four traffic light processes */
    for (int d = 0; d < DIR_COUNT; d++) {
        pids[np] = fork();
        if (pids[np] == 0) {
            run_light(g_mqid, g_shmid, g_semid, d);
            exit(0);
        }
        np++;
    }

    /* 3. Vehicle detector */
    pids[np] = fork();
    if (pids[np] == 0) {
        run_vehicle_detector(g_mqid, g_shmid, g_semid,
                             cfg.vehicle_interval);
        exit(0);
    }
    np++;

    /* 4. Pedestrian */
    pids[np] = fork();
    if (pids[np] == 0) {
        run_pedestrian(g_mqid, g_shmid, g_semid,
                       cfg.ped_interval, cfg.ped_max_wait);
        exit(0);
    }
    np++;

    /* 5. Emergency */
    pids[np] = fork();
    if (pids[np] == 0) {
        run_emergency(g_mqid, g_shmid, g_semid, cfg.emg_interval);
        exit(0);
    }
    np++;

    /* 6. OpenGL display */
    pid_t display_pid = fork();
    if (display_pid == 0) { run_display(g_shmid, g_semid); exit(0); }
    usleep(400000);

    /* 7. Optional timer */
    if (cfg.run_duration > 0) {
        pids[np] = fork();
        if (pids[np] == 0) {
            sleep((unsigned)cfg.run_duration);
            SharedState *s = shm_attach(g_shmid);
            if (s) { s->running = 0; shm_detach(s); }
            exit(0);
        }
        np++;
    }

    /* 8. Control logic runs in this process */
    run_control(g_mqid, g_shmid, g_semid, &cfg);

    /* Shutdown sequence */
    printf("\n[Main] Shutting down...\n");
    SharedState *s = shm_attach(g_shmid);
    if (s) { s->running = 0; shm_detach(s); }
    sleep(2);

    broadcast_shutdown(g_mqid);
    sleep(1);

    kill(display_pid, SIGTERM);
    for (int i = 0; i < np; i++) kill(pids[i], SIGTERM);
    waitpid(display_pid, NULL, 0);
    for (int i = 0; i < np; i++) waitpid(pids[i], NULL, 0);

    cleanup();
    printf("[Main] All resources cleaned up. Goodbye.\n");
    return 0;
}
