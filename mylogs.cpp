#include <syslog.h>
#include <cassert>
#include <vector>
#include <string>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <iostream>

extern std::ostream * default_sink_;

struct mylogger_default_sink
{
    boost::posix_time::ptime pt_open;

    mylogger_default_sink(std::string const & fp)
    boost::posix_time::ptime pt_open;
        : basic_file_sink<char>(filename(fp), std::ios_base::app|std::ios_base::out)
    {
    }
};

struct mylogger // io::stream<string_sink>
{
    explicit mylogger(std::ostream & outs) // : io::stream<string_sink>(*this)
        : outs_(&outs)
    {
        bytes_count_ = 0;
        lins_.resize(1);
    }
    mylogger() // : io::stream<string_sink>(*this)
        : outs_(default_sink_)
    {
        bytes_count_ = 0;
        lins_.resize(1);
    }

    template <typename T> mylogger & operator<<(T const & l)
    {
        lins_.push_back( boost::lexical_cast<std::string>(l) );
        bytes_count_ += lins_.back().size() + 1;
        return *this;
    }

    void commit(int linum, const char *fn, const char *fname)
    {
        BOOST_ASSERT(!lins_.empty());
        std::string tmps;
        tmps.reserve((lins_.front().size() + bytes_count_ + lins_.size() + 4) & 0x0ffffc);
        std::cerr << tmps.capacity() << " capacity\n";

        auto it = lins_.begin();
        tmps = *it++;
        if (!tmps.empty())
            tmps += ":";
        else if (it == lins_.end())
            return;
        else
            tmps = *it++;
        for (; it != lins_.end(); ++it)
        {
            tmps += " ";
            tmps += *it;
        }
        std::cerr << tmps.size() << " size\n";
        BOOST_ASSERT(tmps.size() <= tmps.capacity());

        if (outs_)
        {
            auto t = boost::posix_time::second_clock::local_time();
            *outs_ << t.time_of_day() << ": "
                << tmps
                << " #"<< linum <<":"<< fn <<"\n";
        }
        else
            ::syslog(0, "%.*s #%d:%s", (int)tmps.size(), tmps.c_str(), linum, fn);

        lins_.resize(1);
        bytes_count_ = 0;
    }

    template <typename T> mylogger & prefix(T const & t)
    {
        std::string & ts = lins_.front();
        if (!ts.empty())
            ts += ' ';
        ts += boost::lexical_cast<std::string>(t);
        return *this;
    }

    unsigned int bytes_count_;
    std::vector<std::string> lins_;
    std::ostream * outs_;
};

template <typename T,int Y> struct flipbool_helper : T
{
    mutable int xbool_;

    template <typename ...A> flipbool_helper(A... a) : T(a...) { xbool_=Y; }
    flipbool_helper() { xbool_=Y; }

    bool get_and_flip() const
    {
        bool b = xbool_;
        xbool_ = !b;
        return b;
    }
};

#define LOG for(flipbool_helper<mylogger,1> l(std::clog); (l).get_and_flip(); (l).commit(__LINE__,__FUNCTION__,__FILE__)) (l)
#define LOG_L(l) for(int y=1; y--; (l).commit(__LINE__,__FUNCTION__,__FILE__)) (l)

#if defined(__main__)
struct X {};

std::ostream & operator<<(std::ostream & out, X const &)
{
    return out << "12345X";
}

int main()
{
    LOG << sizeof(mylogger);
    LOG << sizeof(std::string);
    // LOG << sizeof(io::stream<string_sink>);
    LOG << "";
    // LOG << std::endl;
    LOG << "";
    LOG << sizeof(std::clog) << sizeof(std::ostream) << sizeof(mylogger) << sizeof(std::string);

    mylogger slog;
    slog.prefix("hello");
    slog.prefix("world");

    LOG_L(slog) << "f";
    LOG_L(slog) << "fo";
    LOG_L(slog) << "foo";
    LOG_L(slog) << "f" << "fo" << "foo";

    LOG_L(slog) << X();
}

#endif

