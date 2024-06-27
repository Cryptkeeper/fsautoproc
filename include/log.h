#ifndef FSAUTOPROC_LOG_H
#define FSAUTOPROC_LOG_H

// use __FILE_NAME__ if available for a shorter log message
#ifdef __FILE_NAME__
#define LOG_FILE_MACRO __FILE_NAME__
#else
#define LOG_FILE_MACRO __FILE__
#endif

#define log_info(fmt, ...) printf(fmt " \n", __VA_ARGS__)
#define log_verbose(fmt, ...) printf(fmt " \n", __VA_ARGS__)
#define log_error(fmt, ...)                                                    \
  fprintf(stderr, fmt " [%s:%d]\n", __VA_ARGS__, LOG_FILE_MACRO, __LINE__)

#endif//FSAUTOPROC_LOG_H
