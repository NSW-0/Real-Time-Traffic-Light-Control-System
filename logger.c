#include "traffic.h"

static FILE *g_logfile = NULL;

void log_event(int mqid, int direction, const char *msg) {
    TrafficMsg m;
    memset(&m, 0, sizeof m);
    m.mtype     = MTYPE_TO_LOGGER;
    m.direction = direction;
    snprintf(m.info, sizeof m.info, "%s", msg);
    mq_send(mqid, &m);
}

void run_logger(int mqid) {
    g_logfile = fopen("traffic.log", "a");
    if (!g_logfile) g_logfile = stderr;

    char tsbuf[32];
    ts_str(tsbuf, sizeof tsbuf);
    fprintf(g_logfile, "\n=== Traffic system started: %s ===\n", tsbuf);
    fflush(g_logfile);
    printf("[Logger] Started — writing to traffic.log\n");
    fflush(stdout);

    TrafficMsg m;
    while (1) {
        if (mq_recv(mqid, &m, MTYPE_TO_LOGGER) < 0) {
            if (errno == EIDRM || errno == EINVAL) break;
            continue;
        }
        if (m.cmd == CMD_SHUTDOWN) break;

        ts_str(tsbuf, sizeof tsbuf);
        char dirstr[12] = "";
        if (m.direction >= 0 && m.direction < DIR_COUNT)
            snprintf(dirstr, sizeof dirstr, "[%s]", DIR_NAME[m.direction]);

        fprintf(g_logfile, "[%s]%s %s\n", tsbuf, dirstr, m.info);
        fflush(g_logfile);

        /* Print important events in red to terminal */
        if (strstr(m.info,"SAFETY")||strstr(m.info,"EMERGENCY")||
            strstr(m.info,"VIOLATION")||strstr(m.info,"ERROR")) {
            printf("\033[1;31m[LOG] %s\033[0m\n", m.info);
            fflush(stdout);
        }
    }

    ts_str(tsbuf, sizeof tsbuf);
    fprintf(g_logfile, "=== Traffic system stopped: %s ===\n\n", tsbuf);
    if (g_logfile != stderr) fclose(g_logfile);
    printf("[Logger] Stopped.\n");
    fflush(stdout);
}
