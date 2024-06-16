#ifndef FSAUTOPROC_LOG_H
#define FSAUTOPROC_LOG_H

#define log_info(fmt, ...) printf(fmt "\n", __VA_ARGS__)
#define log_verbose(fmt, ...) printf(fmt "\n", __VA_ARGS__)
#define log_error(fmt, ...) fprintf(stderr, fmt "\n", __VA_ARGS__)

#endif//FSAUTOPROC_LOG_H
