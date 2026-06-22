#ifndef TRAFFIC_H
#define TRAFFIC_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <errno.h>

/* ── Directions ──────────────────────────────────────── */
#define DIR_NORTH  0
#define DIR_SOUTH  1
#define DIR_EAST   2
#define DIR_WEST   3
#define DIR_COUNT  4

extern const char *DIR_NAME[DIR_COUNT];

/* ── Light states ────────────────────────────────────── */
#define LIGHT_RED     0
#define LIGHT_YELLOW  1
#define LIGHT_GREEN   2

extern const char *LIGHT_NAME[3];

/* ── Traffic phases ──────────────────────────────────── */
#define PHASE_NS_GREEN    0
#define PHASE_NS_YELLOW   1
#define PHASE_ALL_RED_1   2
#define PHASE_EW_GREEN    3
#define PHASE_EW_YELLOW   4
#define PHASE_ALL_RED_2   5
#define PHASE_PEDESTRIAN  6
#define PHASE_EMERGENCY   7
#define PHASE_COUNT       8

extern const char *PHASE_NAME[PHASE_COUNT];

/* ── IPC Keys ────────────────────────────────────────── */
#define IPC_KEY_MSGQ  0x54521001
#define IPC_KEY_SHM   0x54521002
#define IPC_KEY_SEM   0x54521003

/* ── Message routing (mtype) ─────────────────────────── */
/* 1 = control, 2=North, 3=South, 4=East, 5=West, 6=Logger */
#define MTYPE_TO_CONTROL   1L
#define MTYPE_TO_LIGHT_N   2L
#define MTYPE_TO_LIGHT_S   3L
#define MTYPE_TO_LIGHT_E   4L
#define MTYPE_TO_LIGHT_W   5L
#define MTYPE_TO_LOGGER    6L

static const long MTYPE_FOR_DIR[DIR_COUNT] = {
    MTYPE_TO_LIGHT_N, MTYPE_TO_LIGHT_S,
    MTYPE_TO_LIGHT_E, MTYPE_TO_LIGHT_W
};

/* ── Commands and events ─────────────────────────────── */
#define CMD_SET_RED          1
#define CMD_SET_YELLOW       2
#define CMD_SET_GREEN        3
#define CMD_SHUTDOWN         4
#define EVT_VEHICLE_DETECTED 10
#define EVT_PEDESTRIAN_REQ   11
#define EVT_EMERGENCY_ON     12
#define EVT_EMERGENCY_OFF    13
#define EVT_LIGHT_CHANGED    20
#define EVT_LIGHT_ERROR      21

/* ── The message struct ──────────────────────────────── */
typedef struct {
    long   mtype;
    int    cmd;
    int    direction;
    int    value;
    time_t ts;
    char   info[64];
} TrafficMsg;

/* ── Semaphore ───────────────────────────────────────── */
#define SEM_SHM_WRITE 0
#define SEM_SHM_COUNT 1

static inline int sem_lock(int semid, int idx) {
    struct sembuf op = { (unsigned short)idx, -1, 0 };
    return semop(semid, &op, 1);
}
static inline int sem_unlock(int semid, int idx) {
    struct sembuf op = { (unsigned short)idx, +1, 0 };
    return semop(semid, &op, 1);
}

/* ── Message queue helpers ───────────────────────────── */
static inline int mq_send(int mqid, TrafficMsg *m) {
    m->ts = time(NULL);
    return msgsnd(mqid, m, sizeof(TrafficMsg) - sizeof(long), 0);
}
static inline int mq_recv(int mqid, TrafficMsg *m, long mtype) {
    return msgrcv(mqid, m, sizeof(TrafficMsg) - sizeof(long), mtype, 0);
}
static inline int mq_recv_nb(int mqid, TrafficMsg *m, long mtype) {
    return msgrcv(mqid, m, sizeof(TrafficMsg) - sizeof(long),
                  mtype, IPC_NOWAIT);
}

/* ── Timestamp ───────────────────────────────────────── */
static inline void ts_str(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", t);
}

#endif /* TRAFFIC_H */
