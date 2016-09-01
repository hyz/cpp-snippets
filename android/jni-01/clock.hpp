#ifndef CLOCK_HPP__
#define CLOCK_HPP__

#include <sys/time.h>
#include <time.h>
#include <stdint.h>
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

struct clock_realtime_type {
    struct duration {
        int64_t microseconds;
    };
    struct time_point : timeval {
        duration operator-(time_point const& rhs) const {
            return duration{ int64_t(tv_sec - rhs.tv_sec)*1000000 + (tv_usec - rhs.tv_usec) };
        }
    };
    static time_point middle_night() {
        time_point tp = {};
        struct tm tm;
        time_t t = time(0);
        localtime_r(&t, &tm);
        tm.tm_sec = 0;
        tm.tm_min = 0;
        tm.tm_hour = 0;
        tp.tv_sec = mktime(&tm);
        return tp;
    }
    static time_point now() {
        time_point tp;
        gettimeofday(&tp,NULL);
        return tp;
        //time_point tp;
        //clock_gettime(CLOCK_REALTIME, &tp);
        //return tp;
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

#endif // CLOCK_HPP__

    //return (double)clock() / CLOCKS_PER_SEC;

