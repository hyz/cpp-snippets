#ifndef ALOG_HPP__
#define ALOG_HPP__

#include <android/log.h>

#ifndef LOG_TAG
#   define LOG_TAG
#endif

template <typename... As> void logfn_( int ln, char const* e, char const* c, char const* ltag
        , FILE* fp, char const* fmt, As... a) {
#ifdef ALOG_CONSOLE
    fprintf(fp, fmt, ltag, ln, e, c, a... );
    fputs("\n", fp);
#else
    __android_log_print(stderr==fp ? ANDROID_LOG_ERROR : ANDROID_LOG_DEBUG
            , ltag+2, fmt, "", ln, e, c, a...);
#endif
}
template <typename... A> void err_exit_(A... a) {
    logfn_(a...);
    exit(127);
}
#undef  LOGE
#undef  LOGD
#define LOGD(...)           logfn_(__LINE__,__FUNCTION__," ", "D/" LOG_TAG, stdout, "%s %d:%s%s" __VA_ARGS__)
#define LOGE(...)           logfn_(__LINE__,__FUNCTION__," ", "E/" LOG_TAG, stderr, "%s %d:%s%s" __VA_ARGS__)
#define ERR_MSG(...)        logfn_(__LINE__,__FUNCTION__," ", "E/" LOG_TAG, stderr, "%s %d:%s%s" __VA_ARGS__)
#define ERR_EXIT(...)    err_exit_(__LINE__,__FUNCTION__," ", "E/" LOG_TAG, stderr, "%s %d:%s%s" __VA_ARGS__)
#define ERR_MSG_IF(e, ...)  if(e)   logfn_(__LINE__,__FUNCTION__, "(" #e ") ", "E/" LOG_TAG, stderr,"%s %d:%s%s" __VA_ARGS__)
#define ERR_EXIT_IF(e, ...) if(e)err_exit_(__LINE__,__FUNCTION__, "(" #e ") ", "E/" LOG_TAG, stderr,"%s %d:%s%s" __VA_ARGS__)

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

