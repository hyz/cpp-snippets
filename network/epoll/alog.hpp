#ifndef ALOG_HPP__
#define ALOG_HPP__

# include <stdlib.h>
# include <string.h> // basename(__FILE__);
# include <stdio.h>
#if defined(__ANDROID__)
# include <android/log.h>
#elif !defined(ALOG_CONSOLE)
# define ALOG_CONSOLE
#endif

#if defined(__ANDROID__) && !defined(ALOG_CONSOLE)
  enum { LOG_ERROR = ANDROID_LOG_ERROR };
  enum { LOG_WARN  = ANDROID_LOG_WARN };
  enum { LOG_DEBUG = ANDROID_LOG_DEBUG };
  enum { LOG_VERBO = ANDROID_LOG_VERBOSE };
#else
  enum { LOG_ERROR = 4 };
  enum { LOG_WARN  = 3 };
  enum { LOG_VERBO = 2 };
  enum { LOG_DEBUG = 1 };
#endif

//#if defined(__GCC__) && (__GNUC__ == 4) && (__GNUC_MINOR__ == 4)
//#endif

//#ifndef LOG_TAG
//#   define LOG_TAG
//#endif

template <int V=0>
struct logsettings {
    static FILE* fp;
};
template <int V> FILE* logsettings<V>::fp = stderr;

template <typename... As> int logfn_(int lev, char const*fname, int ln, char const*func, char const* c
        , char const* fmt, As... a)
{
    char tag[64]; {
        char const *src = strrchr(fname, '/');
        if (src)
            ++src;
        else
            src = fname;
        char *p = tag+2, *end = tag+(64-2);
        while (p < end && (*p = *src) && *src != '.') {
            ++src; ++p;
        }
        *p++ = ' ';
        *p = '\0';
    }
#ifdef ALOG_CONSOLE
    //switch (lev) {
    //    case LOG_DEBUG: lev=1 ; break;
    //    case LOG_WARN : lev=2 ; break;
    //    case LOG_ERROR: lev=3 ; break;
    //    default: lev=0 ; break;
    //}
    static char levs[] = { 'X','D','V','W','E' };
    tag[0] = levs[lev]; //((fp == stderr) ? 'E' : 'D');
    tag[1] = '/';
    fprintf(logsettings<>::fp, fmt, tag, ln, func, c, a... );
    fputs("\n", logsettings<>::fp);
    //if (lev >= LOG_WARN) { fflush(logsettings<>::fp); }
#else
    __android_log_print(lev, tag+2, fmt, "", ln, func, c, a...);
#endif
    return 127;
}
//template <typename... A> void err_exit_(A... a) {
//    logfn_(a...);
//    exit(127);
//}
#undef  LOGE
#undef  LOGW
#undef  LOGD
#undef  LOGV
#define LOGV(...)           logfn_(LOG_VERBO,__FILE__,__LINE__,__FUNCTION__,"", "%s%d:%s%s " __VA_ARGS__)
#define DEBUG(...)          logfn_(LOG_DEBUG,__FILE__,__LINE__,__FUNCTION__,"", "%s%d:%s%s " __VA_ARGS__)
#define LOGD(...)           logfn_(LOG_DEBUG,__FILE__,__LINE__,__FUNCTION__,"", "%s%d:%s%s " __VA_ARGS__)
#define LOGW(...)           logfn_(LOG_WARN ,__FILE__,__LINE__,__FUNCTION__,"", "%s%d:%s%s " __VA_ARGS__)
#define LOGE(...)           logfn_(LOG_ERROR,__FILE__,__LINE__,__FUNCTION__,"", "%s%d:%s%s " __VA_ARGS__)
#define ERR_EXIT(...)  exit(logfn_(LOG_ERROR,__FILE__,__LINE__,__FUNCTION__,"", "%s%d:%s%s " __VA_ARGS__))
#define ERR_EXIT_IF(e, ...) if(e)exit(logfn_(LOG_ERROR,__FILE__,__LINE__,__FUNCTION__, ":[" #e "]","%s%d:%s%s " __VA_ARGS__))
#define LOGE_IF(e, ...)     if(e)     logfn_(LOG_ERROR,__FILE__,__LINE__,__FUNCTION__, ":[" #e "]","%s%d:%s%s " __VA_ARGS__)
#define LOGW_IF(e, ...)     if(e)     logfn_(LOG_WARN ,__FILE__,__LINE__,__FUNCTION__, ":[" #e "]","%s%d:%s%s " __VA_ARGS__)
#define LOGD_IF(e, ...)     if(e)     logfn_(LOG_DEBUG,__FILE__,__LINE__,__FUNCTION__, ":[" #e "]","%s%d:%s%s " __VA_ARGS__)

#if 0 //defined(NDEBUG)
# warning "NDEBUG"
# undef DEBUG
# undef LOGD
# define DEBUG(...) ((void)0)
# define LOGD(...) ((void)0)
#endif

inline char const* abi_str()
{
#if defined(__arm__)
#  if defined(__ARM_ARCH_7A__)
#    if defined(__ARM_NEON__)
#      if defined(__ARM_PCS_VFP)
#  define ABI "armeabi-v7a/NEON (hard-float)"
#      else
#  define ABI "armeabi-v7a/NEON"
#      endif
#    else
#      if defined(__ARM_PCS_VFP)
#  define ABI "armeabi-v7a (hard-float)"
#      else
#  define ABI "armeabi-v7a"
#      endif
#    endif
#  else
#  define ABI "armeabi"
#  endif
#elif defined(__i386__)
#  define ABI "x86"
#elif defined(__x86_64__)
#  define ABI "x86_64"
#elif defined(__mips64)  /* mips64el-* toolchain defines __mips__ too */
#  define ABI "mips64"
#elif defined(__mips__)
#  define ABI "mips"
#elif defined(__aarch64__)
#  define ABI "arm64-v8a"
#else
#  define ABI "unknown"
#endif
    return ABI;
}

#endif // ALOG_HPP__

