#ifndef FSAUTOPROC_SYS_H
#define FSAUTOPROC_SYS_H

/// @brief Prints a formatted string to stderr and appends the current system
/// error message, if any. (i.e. `perror` with a format string)
/// @param fmt The format string to print.
/// @param ... The format arguments.
void perrorf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#endif//FSAUTOPROC_SYS_H
