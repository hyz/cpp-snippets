#include <boost/icl/ptime.hpp> 
#include <string>
#include <fstream>
#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>
typedef unsigned int UInt;

using namespace std;
using namespace boost::posix_time;
using namespace boost::icl;

typedef std::set<UInt> set_type;

template <typename K>
interval<time_t>::type time_interval(K const& k, char*const s_b, char*const s_e)
{
    struct tm lt0;
    struct tm bt = {0};
    struct tm et = {0};

    time_t lo = k.lower();
    localtime_r(&lo, &lt0);

    strptime(s_b, "%H:%M", &bt);
    strptime(s_e, "%H:%M", &et);

    bt.tm_sec = et.tm_sec = 0 ;
    bt.tm_year = et.tm_year = lt0.tm_year ;
    bt.tm_yday = et.tm_yday = lt0.tm_yday ;
    bt.tm_mon = et.tm_mon = lt0.tm_mon ;
    bt.tm_mday = et.tm_mday = lt0.tm_mday ;
    bt.tm_wday = et.tm_wday = lt0.tm_wday ;

    return interval<time_t>::right_open(mktime(&bt) , mktime(&et) + (s_b==s_e?60*60:0));
}

template <typename T>
void load(T& onls, char const* f)
{
    std::string line;
    std::ifstream in(f);
    while (getline(in, line)) {
        if (!boost::ends_with(line, "ONL"))
            continue;
        std::istringstream ins(line);
        UInt uid, b, e;
        ins >> uid >> b >> e;
        onls += std::make_pair( interval<time_t>::right_open(b, e), set_type{uid} );
    }
}

int main(int argc, char* const argv[])
{
    if (argc < 3)
        return 1;
    interval_map<time_t, set_type> onls;

    load(onls, argv[1]);

    if (onls.empty()) {
        return 0;
    }
    {
        auto it = onls.begin();
        cout << it->first.lower() <<" "<< it->first.upper() << endl;
        it = onls.end();
        --it;
        cout << it->first.lower() <<" "<< it->first.upper() << endl;
    }

    interval<time_t>::type tinv = time_interval(onls.begin()->first, argv[2], argc==3?argv[2]:argv[3]);

    set_type sum;
    auto p = onls.equal_range( tinv );
    for (auto it = p.first; it != p.second; ++it) {
        for (auto& x : it->second)
            sum.insert(x); // p.second;
    }

    time_t lo = tinv.lower();
    time_t up = tinv.upper();
    char lobuf[64], upbuf[64];
    cout << ctime_r(&lo, lobuf) <<""<< ctime_r(&up, upbuf) <<" "<< sum.size() << endl;

    return 0;
}

