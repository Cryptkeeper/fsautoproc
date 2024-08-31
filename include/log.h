/// @file log.h
/// @brief Logging macros for the application.
#ifndef FSAUTOPROC_LOG_H
#define FSAUTOPROC_LOG_H

/// @def LOG_FILE_MACRO
/// @brief The file name macro used in log messages. Value varies depending on
/// the availability of the shorter `__FILE_NAME__` macro.
#ifdef __FILE_NAME__
#define LOG_FILE_MACRO __FILE_NAME__
#else
#define LOG_FILE_MACRO __FILE__
#endif

/// @def log_info
/// @brief Log an informational message.
/// @note A newline is appended to the message.
#define log_info(fmt, ...) printf(fmt " \n", __VA_ARGS__)

/// @def log_verbose
/// @brief Log a verbose message. The caller is responsible for checking the
/// verbose flag before calling this macro.
/// @note A newline is appended to the message.
#define log_verbose(fmt, ...) printf(fmt " \n", __VA_ARGS__)

/// @def log_error
/// @brief Log an error message to stderr. This will not terminate the program.
/// @note A newline is appended to the message.
#define log_error(fmt, ...)                                                    \
  fprintf(stderr, "[!] " fmt " [%s:%d]\n", __VA_ARGS__, LOG_FILE_MACRO,        \
          __LINE__)

#endif//FSAUTOPROC_LOG_H
