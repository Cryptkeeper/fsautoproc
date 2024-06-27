#ifndef FSAUTOPROC_LOG_H
#define FSAUTOPROC_LOG_H

#define log_info(fmt, ...) printf(fmt " \n", __VA_ARGS__)
#define log_verbose(fmt, ...) printf(fmt " \n", __VA_ARGS__)
#define log_error(fmt, ...)                                                    \
  fprintf(stderr, fmt " [%s:%d]\n", __VA_ARGS__, __FILE_NAME__, __LINE__)

#endif//FSAUTOPROC_LOG_H
