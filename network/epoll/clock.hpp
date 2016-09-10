#ifndef CLOCK_HPP__
#define CLOCK_HPP__

#include <sys/time.h>
#include <time.h>
#include <stdint.h>

#if 0
#include <boost/chrono/process_cpu_clocks.hpp>

namespace chrono = boost::chrono;

typedef chrono::process_real_cpu_clock process_real_cpu_clock;

inline unsigned microseconds(process_real_cpu_clock::duration const& d) {
    return chrono::duration_cast<chrono::microseconds>(d).count();
}
inline unsigned milliseconds(process_real_cpu_clock::duration const& d) {
    return chrono::duration_cast<chrono::milliseconds>(d).count();
}
inline unsigned seconds(process_real_cpu_clock::duration const& d) {
    return chrono::duration_cast<chrono::seconds>(d).count();
}
#endif

struct clock_realtime_type {
    struct duration {
        int64_t microseconds;
    };
    struct time_point : timeval {
        duration operator-(time_point const& rhs) const {
            return duration{ int64_t(tv_sec - rhs.tv_sec)*1000000 + (tv_usec - rhs.tv_usec) };
        }
        time_point(time_t sec, unsigned usec=0) { tv_sec = sec; tv_usec = usec; }
    };
    static time_point midnight() {
        return time_point(midnight_time(), 0);
    }
    static time_point now() {
        time_point tp(0);
        gettimeofday(&tp,NULL); //clock_gettime(CLOCK_REALTIME, &tp);
        //DEBUG("gettimeofday %u %u", tp.tv_sec, tp.tv_usec);
        return tp;
    }
    static time_point epoch() { return time_point(0,0); }

    static time_t midnight_time() {
        static time_t midt = 0;
        if (midt == 0) {
            struct tm tm;
            time_t t = time(0);
            localtime_r(&t, &tm);
            //DEBUG("tm %u %u:%u:%u", (unsigned)t, tm.tm_hour,tm.tm_min,tm.tm_sec);
            tm.tm_sec = 0;
            tm.tm_min = 0;
            tm.tm_hour = 0;
            return (midt = mktime(&tm));
        }
        return midt;
    }
};
inline unsigned microseconds(clock_realtime_type::duration const& d) {
    return d.microseconds;
}
inline unsigned milliseconds(clock_realtime_type::duration const& d) {
    return d.microseconds/1000;
}
inline unsigned seconds(clock_realtime_type::duration const& d) {
    return d.microseconds/1000000;
}

//inline int ms_since_midnight() {
//    //get high precision time
//    timespec now;
//    clock_gettime(CLOCK_REALTIME,&now);
//
//    //get low precision local time
//    time_t now_local = time(NULL);
//    struct tm* lt = localtime(&now_local);
//
//    //compute time shift utc->est
//    int sec_local = lt->tm_hour*3600+lt->tm_min*60+lt->tm_sec;
//    int sec_utc = static_cast<long>(now.tv_sec) % 86400;
//    int diff_sec; //account for fact utc might be 1 day ahead
//    if(sec_local<sec_utc) diff_sec = sec_utc-sec_local;
//    else diff_sec = sec_utc+86400-sec_local;
//    int diff_hour = (int)((double)diff_sec/3600.0+0.5); //round to nearest hour
//
//    //adjust utc to est, round ns to ms, add
//    return (sec_utc-(diff_hour*3600))*1000+(int)((static_cast<double>(now.tv_nsec)/1000000.0)+0.5);
//}
//unsigned msecs_since_midnight() {
//    struct timespec tsv;
//    clock_gettime(CLOCK_REALTIME, &tsv);
//    bool within_a_day = (tsv.tv_sec < (global_midnight + 24*3600));
//    if (within_a_day)
//        if (checked_2am || (tsv.tv_sec < (global_midnight + 2*3600)))
//            return ((tsv.tv_sec - global_midnight)*1000 + (tsv.tv_nsec + 500000)/1000000);
//    update_global_midnight(tsv.tv_sec, within_a_day);
//    return ((tsv.tv_sec - global_midnight)*1000 + (tsv.tv_nsec + 500000)/1000000);
//}

struct timestamp : tm {
    time_t time;
    timestamp() {
        this->time = ::time(0);
        localtime_r(&time, this);
    }
    timestamp* uptime() {
        time_t ct = ::time(0);
        unsigned x = ct - time;
        if (x + tm_sec > 59) {
            localtime_r(&time, this);
        }
        tm_sec += x;
        time = ct;
        return this;
    }
};

struct uptimeval : timeval {
    uptimeval() {
        gettimeofday(this,NULL); //clock_gettime(CLOCK_REALTIME, &tp);
    }
    struct timeval up() {
        timeval tp;
        gettimeofday(&tp,NULL); //clock_gettime(CLOCK_REALTIME, &tp);
        if (tp.tv_usec < tv_usec) {
            tp.tv_usec += 1000000 - tv_usec;
            --tp.tv_sec;
        } else {
            tp.tv_usec -= tv_usec;
        }
        tp.tv_sec -= tv_sec;
        return tp;
    }
};

#endif // CLOCK_HPP__

    //return (double)clock() / CLOCKS_PER_SEC;

