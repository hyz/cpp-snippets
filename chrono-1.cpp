#include <chrono>
#include <ctime>
#include <iostream>

int
main()
{
    using namespace std;
    using namespace std::chrono;
    typedef duration<int, ratio_multiply<hours::period, ratio<24> >::type> days;
    system_clock::time_point now = system_clock::now();
    system_clock::duration tp = now.time_since_epoch();
    days d = duration_cast<days>(tp);
    tp -= d;
    hours h = duration_cast<hours>(tp);
    tp -= h;
    minutes m = duration_cast<minutes>(tp);
    tp -= m;
    seconds s = duration_cast<seconds>(tp);
    tp -= s;
    std::cout << d.count() << "d " << h.count() << ':'
              << m.count() << ':' << s.count();
    std::cout << " " << tp.count() << "["
              << system_clock::duration::period::num << '/'
              << system_clock::duration::period::den << "]\n";

    time_t tt = system_clock::to_time_t(now);
    tm utc_tm = *gmtime(&tt);
    tm local_tm = *localtime(&tt);
    std::cout << utc_tm.tm_year + 1900 << '-';
    std::cout << utc_tm.tm_mon + 1 << '-';
    std::cout << utc_tm.tm_mday << ' ';
    std::cout << utc_tm.tm_hour << ':';
    std::cout << utc_tm.tm_min << ':';
    std::cout << utc_tm.tm_sec << '\n';
}

std::chrono::system_clock::duration duration_since_midnight() {
    auto now = std::chrono::system_clock::now();

    time_t tnow = std::chrono::system_clock::to_time_t(now);
    tm *date = std::localtime(&tnow);
    date->tm_hour = 0;
    date->tm_min = 0;
    date->tm_sec = 0;
    auto midnight = std::chrono::system_clock::from_time_t(std::mktime(date));

    return now-midnight;
}

int main_2()
{
    auto since_midnight = duration_since_midnight();

    auto hours = std::chrono::duration_cast<std::chrono::hours>(since_midnight);
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(since_midnight - hours);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_midnight - hours - minutes);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(since_midnight - hours - minutes - seconds);
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(since_midnight - hours - minutes - seconds - milliseconds);
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(since_midnight - hours - minutes - seconds - milliseconds - microseconds);

    std::cout << hours.count() << "h ";
    std::cout << minutes.count() << "m ";
    std::cout << seconds.count() << "s ";
    std::cout << milliseconds.count() << "ms ";
    std::cout << microseconds.count() << "us ";
    std::cout << nanoseconds.count() << "ns\n";
    return 0;
}


#include <chrono>  // chrono::system_clock
#include <ctime>   // localtime
#include <sstream> // stringstream
#include <iomanip> // put_time
#include <string>  // string

std::string return_current_time_and_date()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    return ss.str();
}
