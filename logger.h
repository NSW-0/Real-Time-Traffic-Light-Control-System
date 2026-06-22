#ifndef LOGGER_H
#define LOGGER_H
void log_event(int mqid, int direction, const char *msg);
void run_logger(int mqid);
#endif
