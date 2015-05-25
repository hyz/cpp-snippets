#include <iostream>
#include <time.h>
#include <boost/date_time/posix_time/posix_time.hpp>

using namespace boost::posix_time;

void time_t_test()
{
    time_t t1 = time(0);
    {
        struct tm* tm = localtime(&t1);
        time_t t2 = mktime(tm);
        std::cout << t1 <<" "<< t2 << "\n";
    }
    {
        struct tm* tm = gmtime(&t1);
        time_t t2 = mktime(tm);
        std::cout << t1 <<" "<< t2 << "\n";
    }
}

int main()
{
    std::cout << "second_clock::local_time().date()\n";
    ptime t0(second_clock::local_time().date());
    ptime t1(second_clock::universal_time().date());
    std::cout << t0 <<" "<< t1 << "\n";

    std::cout << "microsec_clock::local_time\n";
    {
        ptime t(microsec_clock::local_time());
        std::cout << t - t0 << "\n";
        std::cout << t <<" "<< (t-t0).total_milliseconds() <<"\n";
    }
    std::cout << "second_clock::universal_time\n";
    {
        ptime t(second_clock::universal_time());
        std::cout << t - t0 << "\n";
        std::cout << t <<" "<< (t-t0).total_milliseconds() <<"\n";
    }
    std::cout << "from_time_t\n";
    {
        ptime t = from_time_t(time(0));
        std::cout << t - t0 << "\n";
        std::cout << t <<" "<< (t-t0).total_milliseconds() <<"\n";
    }
    std::cout << "time_t localtime gmtime mktime\n";
    time_t_test();

    return 0;
}

