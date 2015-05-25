#include <syslog.h>
#include <list>
#include <string>
#include <iostream>
#include <boost/asio/streambuf.hpp>

namespace log {

struct logstream
{
    explicit logstream(int pri = LOG_DEBUG)
        : pri_(pri)
    {}

    ~logstream();

    template <typename T>
    const logstream& operator<< (const T& x) const
    {
        *clog_ << x;
        return *this;
    }

    logstream& operator<<(std::ostream& (*pf)(std::ostream&))
    {
        *clog_ << pf;
        return *this;
    }

    int pri_;
    static boost::asio::streambuf logsbuf_;
    static std::ostream* clog_;
};

void initlog(std::ostream& log)
{
    log.rdbuf(&logstream::logsbuf_);
    logstream::clog_ = &log;
}

inline logstream debug()   { return logstream(LOG_USER | LOG_DEBUG); }
inline logstream info()    { return logstream(LOG_USER | LOG_INFO); }
inline logstream warning() { return logstream(LOG_USER | LOG_WARNING); }
inline logstream err()     { return logstream(LOG_USER | LOG_ERR); }

} // namespace

using namespace std;
using namespace boost;

namespace log {

boost::asio::streambuf logstream::logsbuf_;
std::ostream* logstream::clog_;

logstream::~logstream()
{
    size_t n = 0;
    asio::streambuf::const_buffers_type bufs = logsbuf_.data();
    asio::streambuf::const_buffers_type::const_iterator i = bufs.begin();
    for (; i != bufs.end(); ++i)
    {
        const void *p = asio::detail::buffer_cast_helper(*i);
        size_t len = asio::detail::buffer_size_helper(*i);

        // cout.write(static_cast<const char*>(p), len);
        // cout << endl;
        syslog(pri_, "%.*s", (int)len, (char*)p);
        n += len;
    }

    logsbuf_.consume(n);
}

}

int main()
{
    time_t now = time(0);
    log::initlog(clog);

    log::debug() << "hello debug" << ctime(&now);
    log::info() << "hello info" << ctime(&now);
    log::warning() << "hello warning" << ctime(&now);
    log::err() << "hello err" << ctime(&now);

    return 0;
}

