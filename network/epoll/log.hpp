#ifndef LOG_HPP__
#define LOG_HPP__

#if defined(ANDROID)
#include <android/log.h>
// ANDROID_LOG_ERROR : ANDROID_LOG_DEBUG
// __FILE__
// template <typename... A> inline void log(int lv, A... a) { __android_log_print(lv, "Thread", a...); }

#else // !(ANDROID)
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>

inline int DEBUG(char const*fmt, ...)
{
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n + fputs("\n", stdout);
}
inline int ERR_MSG(char const*fmt, ...)
{
    int n, err = errno;
    va_list ap;
    va_start(ap, fmt);
    n = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return n + fprintf(stderr, ": %s\n", err?strerror(err):"");
}
inline void ERR_EXIT(char const*fmt, ...)
{
    int n, err = errno;
    va_list ap;
    va_start(ap, fmt);
    n = vfprintf(stderr, fmt, ap);
    va_end(ap);
    n += fprintf(stderr, ": %s\n", err?strerror(err):"");
    exit(127);
}
#endif // else // !(ANDROID)

#endif // LOG_HPP__

