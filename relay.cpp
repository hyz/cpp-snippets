
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/system/error_code.hpp>
// #include <boost/ptr_container/ptr_map.hpp>
// #include <boost/ptr_container/ptr_vector.hpp>
#include <boost/asio.hpp>
#include "util.h"

namespace sys = boost::system;
namespace placeholders = boost::asio::placeholders;
using boost::asio::ip::tcp;

void listen(tcp::acceptor & a, tcp::endpoint const & endpoint)
{
    a.open(endpoint.protocol());
    a.set_option(tcp::acceptor::reuse_address(true));
    a.bind(endpoint);

    a.listen();
    // LOG_I << "Listen " << a.local_endpoint();
    //a.async_accept(t_.socket(), bind(&handle_accept, this, placeholders::error));
}

tcp::endpoint resolve(boost::asio::io_service & io_service, const std::string& h, const std::string& p)
{
    tcp::resolver resolver(io_service);
    tcp::resolver::query query(h, p);
    tcp::resolver::iterator i = resolver.resolve(query);
    return *i;
}

//----------------------------------------------------------------------

template <class connection>
struct relay_server : tcp::acceptor
{
    typedef relay_server<connection> this_type;

    typedef boost::shared_ptr<boost::asio::deadline_timer> timer;

    typedef boost::shared_ptr<connection> shared;
    typedef typename std::map<int,shared>::iterator iterator;

    typedef typename connection::message message;
    // typedef typename connection::callback callback;
    typedef boost::function<void (message const &, connection&)> messager_fn;
    typedef boost::function<messager_fn (message const &, connection&)> messager_fn0;

    explicit relay_server(boost::asio::io_service & ios)
        : tcp::acceptor(ios)
    {
    }

    void start(tcp::endpoint const & ep, messager_fn0 const & fn)
    {
        messager0_ = fn;
        ::listen(*this, ep); 

        c_.reset(new connection(*this));
        async_accept(c_->socket(), boost::bind(&this_type::handle_accept, this, placeholders::error));
    }

    template <typename F> void post(F const & fn) { get_io_service().post(fn); }

    this_type::timer new_timer()
    {
        typedef typename timer::element_type T;
        return timer(new T(get_io_service()));
    }

    template <typename F> void post(this_type::timer & t, int seconds, F const & fn)
    {
        t->expires_from_now(boost::posix_time::seconds(seconds));
        t->async_wait(boost::bind(fn, t));
    }

public:
    //friend class connection;

    static void reset(shared p) { p.reset(); }

    iterator destroy(iterator i)
    {
        if (i == cs_.end())
            return i;

        shared sp = i->second;

        cs_.erase(i);
        post(boost::bind(&this_type::reset, sp));

        return cs_.end();
    }

    messager_fn handle_message(message const & m, connection & c) { return messager0_(m, c); }

private:
    std::map<int,shared> cs_;
    shared c_;

    messager_fn0 messager0_;

    void handle_error(sys::error_code const & ec)
    {
        std::cout << ec << std::endl;
    }

    void handle_accept(sys::error_code const & ec)
    {
        if (ec)
        {
            return handle_error(ec);
        }

        std::pair<iterator,bool> ret = cs_.insert( std::make_pair(c_->socket().native_handle(), c_) );
        BOOST_ASSERT (ret.second);
        c_->ready( *this, ret.first );

        c_.reset(new connection(*this));
        async_accept(c_->socket(), boost::bind(&this_type::handle_accept, this, placeholders::error));
    }
};

inline std::string unescape(std::string::const_iterator b, std::string::const_iterator end)
{
    return urldecode(std::string(b, end));
}

template <typename Params>
static bool path_params(std::string& path, Params& params, const std::string& fullpath)
{
    std::string::const_iterator i, b, eop, end;

    i = fullpath.begin();
    if (*i == '/')
        ++i;
    end = std::find(i, fullpath.end(), '#');

    b = std::find(i, end, '?');
    if (b == i)
        return false;

    path = std::string(i, b);

    while (b != end)
    {
        ++b;
        eop = std::find(b, end, '&');
        i = std::find(b, eop, '=');
        if (i != eop)
        {
            params.insert(make_pair(unescape(b, i), unescape(i+1, eop)));
        }

        b = eop;
    }

    return true;
}

struct keyvalues : private std::list<std::pair<std::string, std::string> >
{
    using std::list<std::pair<std::string, std::string> >::iterator;

    iterator find(const std::string& k) const
    {
        return const_cast<keyvalues*>(this)->find(k);
    }
    iterator find(const std::string& k)
    {
        iterator i = begin();
        for (; i != end(); ++i)
            if (i->first == k)
                break;
        return i;
    }

    const std::string& get(const std::string& k, const std::string& defa) const
    {
        iterator i = find(k);
        return (i == end() ? defa : i->second);
    }
    const std::string& get(const std::string& k) const
    {
        iterator i = find(k);
        if (i == end())
            throw std::runtime_error("key not found");
        return i->second;
    }

    iterator insert(const value_type & kv)
    {
        return std::list<std::pair<std::string, std::string> > ::insert(end(), kv);
    }
};

namespace http { namespace error {
    enum http_parse_error
    {
        empty_lines,
        invalid_header_line,
        invalid_path_params,
        invalid_status_line,
    };
} }

namespace boost { namespace system {
    template<> struct is_error_code_enum<http::error::http_parse_error>
    {
        static const bool value = true;
    };
} }

sys::error_code make_error_code(http::error::http_parse_error e)
{
    return sys::error_code();
}

struct request
{
    request()
    {
    }

    explicit request(sys::error_code const & ec)
        : ec_(ec)
    {
    }

    sys::error_code const & error_code() const { return ec_; }

    sys::error_code parse_status_line(std::streambuf & sb)
    {
        using namespace boost;
        sys::error_code ec;

        std::istream ins(&sb);
        std::string line;
        while (getline(ins, line)) // while (ins >> message_.method_)
        {
            if (!boost::all(line, is_space()))
                break;
        }
        if (!ins)
        {
            return make_error_code(http::error::empty_lines);
        }

        regex e("^\\s*(GET|POST)\\s+([^\\s]+)\\s+(HTTP/1.[01])\\s*$");
        smatch what;
        if (!regex_match(line, what, e))
        {
            return make_error_code(http::error::invalid_status_line);
        }

        // ver_ = what[3];
        method_ = what[1];

        if (!path_params(path_, params_, what[2]))
        {
            return make_error_code(http::error::invalid_path_params);
        }

        return ec;
    }

    sys::error_code parse_header_line(const std::string & line)
    {
        using namespace boost;
        sys::error_code ec;

        regex expr("^\\s*([^:]+)\\s*:\\s*(.*?)\\s*$");
        smatch res;
        if (!regex_match(line, res, expr))
        {
            return (make_error_code(http::error::invalid_header_line));
        }

        std::string k(res[1].first, res[1].second);
        std::string val(res[2].first, res[2].second);

        if (boost::iequals(k, "Content-Length"))
        {
            content_.resize(boost::lexical_cast<int>(val));
        }

        headers_.insert( std::make_pair(k, val) );

        return ec;
    }

    std::string & content() { return content_; }

    sys::error_code const & error() const { return ec_; }

    std::string encode() const { return std::string(); }

private:
    // short ver_;
    std::string method_;
    std::string path_;

    keyvalues params_;
    keyvalues headers_;

    std::string content_;

    sys::error_code ec_;
};

struct connection : tcp::socket
{
    typedef connection this_type;
    typedef request message;

    typedef boost::function<void (message const&, connection&)> message_handler;

    typedef relay_server<connection> server;

    connection(server & s)
        : tcp::socket(s.get_io_service())
        , deadline_(s.get_io_service())
    {
    }

    ~connection()
    {
        if (server_)
        {
            sys::error_code ec = boost::asio::error::make_error_code(boost::asio::error::connection_aborted);
            _request(message(ec));

            close();
        }
    }

    tcp::socket & socket() { return *this; }

    connection::server & get_server() { BOOST_ASSERT(server_); return *server_; }

    void close()
    {
        if (server_)
        {
            iself_ = server_->destroy(iself_);
            server_ = 0;
        }
        messager_ = message_handler();
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    void
    send(const ConstBufferSequence& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler)
    {
        boost::asio::async_write(socket(), buffers, handler);
    }

private:
    friend struct relay_server<connection>;

    void ready(server & s, server::iterator iter)
    {
        server_ = &s;
        iself_ = iter;

        boost::asio::async_read_until(socket(), rsbuf_, "\r\n"
                , boost::bind(&this_type::handle_method_line, this, placeholders::error));
    }

    void handle_method_line(sys::error_code const & ecc)
    {
        if (ecc)
        {
            return handle_error(ecc);
        }

        sys::error_code ec;
        if ( (ec = message_.parse_status_line(rsbuf_)))
        {
            if (ec == http::error::empty_lines)
            {
                boost::asio::async_read_until(socket(), rsbuf_, "\r\n"
                        , boost::bind(&this_type::handle_method_line, this, placeholders::error));
                return;
            }
            return handle_error(ec);
        }

        boost::asio::async_read_until(socket(), rsbuf_, "\r\n\r\n"
                , boost::bind(&this_type::handle_headers, this, placeholders::error));

    }

    void handle_headers(sys::error_code const & ecc)
    {
        if (ecc)
        {
            return handle_error(ecc);
        }

        sys::error_code ec;
        std::istream ins(&rsbuf_);
        std::string line;
        while (getline(ins, line) && !line.empty() && line[0] != '\r')
        {
            if ( (ec = message_.parse_header_line(line)))
            {
                return handle_error(ec);
            }
        }

        if (message_.content().size() <= rsbuf_.size())
        {
            return handle_content(ec);
        }

        boost::asio::async_read(socket() , rsbuf_
                , boost::asio::transfer_at_least(message_.content().size() - rsbuf_.size())
                , boost::bind(&this_type::handle_content, this, placeholders::error));
    }

    void handle_content(sys::error_code const & ec)
    {
        if (ec)
        {
            return handle_error(ec);
        }

        rsbuf_.sgetn(&message_.content()[0], message_.content().size());

        _request(message_);

        if (!server_)
        {
            return;
        }
        boost::asio::async_read_until(socket(), rsbuf_, "\r\n"
                , boost::bind(&this_type::handle_method_line, this, placeholders::error));
    }

private:
    server* server_;
    server::iterator iself_;

    boost::asio::deadline_timer deadline_;

    boost::asio::streambuf rsbuf_;

    message_handler messager_;
    message message_;

    void handle_error(sys::error_code const & ec)
    {
        _request(message(ec));

        std::cout << ec << std::endl;
        iself_ = server_->destroy(iself_);
    }

    void _request(message const & msg)
    {
        if (!server_)
            return;
        if (!messager_)
        {
            messager_ = server_->handle_message(msg, *this);
            return;
        }
        messager_(msg, *this);
    }

};

struct apple_push
{
    typedef apple_push this_type;

    tcp::socket socket_;
    tcp::endpoint endpoint_;

    struct stage {
        enum type {
            zero = 0,
            connecting = 1,
            ready,
            writing,
            replying,
        };
    };
    stage::type stage_;

    typedef std::list<std::pair<connection*,std::string> > bufs_type;
    bufs_type bufs_;

    std::string replybuf_;

    boost::asio::streambuf rdbuf_;

    apple_push(boost::asio::io_service & io_service, tcp::endpoint const & ep)
        : socket_(io_service)
        , endpoint_(ep)
    {
        stage_ = stage::zero;
        connect();
    }

    connection::message_handler push(connection::message const& m, connection& c)
    {
        pushmsg(m, c);
        return boost::bind(&apple_push::pushmsg, this, _1, _2);
    }

private:
    void pushmsg(connection::message const & m, connection & c)
    {
        if (m.error()) // = (boost::asio::error::connection_aborted);
        {
            bufs_type::iterator i = bufs_.begin();
            for (; i != bufs_.end(); ++i)
                if (&c == i->first)
                    break;
            if (i == bufs_.end())
                return;

            if (i == bufs_.begin())
            {
                i->first = 0;
                return;
            }
            bufs_.erase(i);
            return;
        }

        bufs_.push_back( make_pair(&c, m.encode()) );

        if (stage_ == stage::ready)
        {
            write_msg();
        }
        else if (stage_ < stage::ready)
        {
            connect();
        }
    }

    void connect()
    {
        stage_ = stage::connecting;
        sys::error_code ec;
        socket_.close(ec);
        socket_.open(boost::asio::ip::tcp::v4());
        socket_.async_connect(endpoint_, boost::bind(&this_type::handle_connect, this, placeholders::error));
    }

    void handle_connect(sys::error_code const & ec)
    {
        stage_ = stage::zero;
        if (ec)
        {
            return handle_error(ec);
        }

        stage_ = stage::ready;

        boost::asio::async_read(socket_, rdbuf_
                , boost::asio::transfer_at_least(32)
                , boost::bind(&this_type::handle_read, this, placeholders::error));

        write_msg();
    }

    void handle_read(sys::error_code const & ec)
    {
        if (ec)
        {
            std::cout << ec << std::endl;
            connect();
            return;
        }

        std::cout << &rdbuf_ << std::endl;
        boost::asio::async_read(socket_, rdbuf_
                , boost::asio::transfer_at_least(32)
                , boost::bind(&this_type::handle_read, this, placeholders::error));
    }

    void write_msg()
    {
        if (stage_ != stage::ready || bufs_.empty())
            return;

        stage_ = stage::writing;
        boost::asio::async_write(socket_
                , boost::asio::buffer(bufs_.front().second)
                , boost::bind(&this_type::handle_write, this, placeholders::error));
    }

    void handle_write(sys::error_code const & ec)
    {
        BOOST_ASSERT (!bufs_.empty());

        stage_ = stage::ready;

        if (bufs_.empty())
        {
            return;
        }

        if (ec)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;

            // http::response rsp(400);
            // replybuf_ = rsp.encode();
            replybuf_ = "FAIL";
        }
        else
        {
            replybuf_ = "OK";
        }

        if (bufs_.front().first)
        {
            stage_ = stage::replying;
            bufs_.front().first->send(boost::asio::buffer(replybuf_)
                    , boost::bind(&this_type::handle_reply, this, placeholders::error));
        }
        else
        {
            handle_reply(sys::error_code());
        }
    }

    void handle_reply(sys::error_code const & ec)
    {
        stage_ = stage::ready;
        bufs_.pop_front();
        write_msg();
    }

    void handle_error(sys::error_code const & ec)
    {
        std::cout << ec << std::endl;
    }
};

int main(int argc, char* argv[])
{
    std::string addr = "127.0.0.1";
    std::string port = "9991";

    std::string ap_addr = "127.0.0.1";
    std::string ap_port = "9991";

    try
    {
        boost::asio::io_service io_service;

        apple_push ap(io_service, resolve(io_service, ap_addr, ap_port));
        relay_server<connection> s(io_service);

        s.start(resolve(io_service, addr, port), boost::bind(&apple_push::push, &ap, _1, _2));

        io_service.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}


