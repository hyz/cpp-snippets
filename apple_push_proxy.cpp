
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
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/format.hpp>
#include "util.h"
#include "jss.h"
#include "log.h"

#define APPLE_HOST          "gateway.sandbox.push.apple.com"
#define APPLE_PORT          "2195"
#define APPLE_FEEDBACK_HOST "feedback.sandbox.push.apple.com"
#define APPLE_FEEDBACK_PORT "2196"
#define RSA_CLIENT_KEY      "PushChatKey.pem"
#define RSA_CLIENT_CERT     "PushChatCert.pem"

namespace sys = boost::system;
namespace placeholders = boost::asio::placeholders;
using boost::asio::ip::tcp;

void listen(tcp::acceptor & a, tcp::endpoint const & endpoint)
{
    a.open(endpoint.protocol());
    a.set_option(tcp::acceptor::reuse_address(true));
    a.bind(endpoint);

    a.listen();
    LOG_I << "listen " << a.local_endpoint();
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
        LOG_I << ec;
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
    typedef std::list<std::pair<std::string, std::string> > base_type;

    using base_type::iterator;
    // using base_type::const_iterator;

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
        return base_type::insert(end(), kv);
    }

    iterator begin() const { return const_cast<keyvalues*>(this)->begin(); }
    iterator begin() { return base_type::begin(); }
    iterator end() const { return const_cast<keyvalues*>(this)->end(); }
    iterator end() { return base_type::end(); }
};

struct response
{
    explicit response(int status = 200);

    void status(int c) { status_code_ = c; }
    int status() const { return status_code_; }

    void header(const std::string& k, const std::string& val);

    void content(const json::object& jso) { content_ = json::encode(jso); }
    void content(const std::string& cont) { content_ = cont; }
    void content(const boost::format& fmt) { content_ = boost::str(fmt); }

    std::string encode() const;
    std::string encode_header() const;

private:
    int status_code_;
    keyvalues headers_;
    std::string content_;

    static const char* code_str(int code);
};

response::response(int status)
{
    status_code_ = status;
    header("Content-Type", "text/plain;charset=UTF-8");
}

void response::header(const std::string& k, const std::string& val)
{
    for (keyvalues::iterator i = headers_.begin(); i != headers_.end(); ++i)
    {
        if (i->first == k)
        {
            i->second = val;
            return;
        }
    }
    headers_.insert(std::make_pair(k, val));
}

const char* response::code_str(int code)
{
    static struct { int code; const char* str; } ecl[] = {
        { 200, "OK" }
        , { 400, "Bad Request" }
        , { 401, "Unauthorized" }
        , { 403, "Forbidden" }
        , { 404, "Not Found" }
        , { 500, "Internal Server Error" }
    };
    for (unsigned int i = 0; i < sizeof(ecl)/sizeof(ecl[0]); ++i)
        if (ecl[i].code == code)
            return ecl[i].str;
    return "Unknown";
}

std::string response::encode() const
{
    return encode_header() + "\r\n" + content_;
}

std::string response::encode_header() const
{
    std::string hdrs;

    for (keyvalues::iterator it = headers_.begin(); it != headers_.end(); ++it)
    {
        hdrs += it->first + ": " + it->second + "\r\n";
    }
    hdrs += "Content-Length: " + boost::lexical_cast<std::string>(content_.size()) + "\r\n";

    return str(boost::format("HTTP/1.1 %d %s\r\n") % status_code_ % code_str(status_code_)) + hdrs;
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

    std::string const & method() const { return method_; }
    std::string const & path() const { return path_; }
    keyvalues const & params() const { return params_; }
    keyvalues const & headers() const { return headers_; }
    std::string const & content() const { return content_; }

    void content(std::streambuf & sbuf) { sbuf.sgetn(&content_[0], content_.size()); }

    sys::error_code const & error() const { return ec_; }

private:
    // short ver_;
    std::string method_;
    std::string path_;

    keyvalues params_;
    keyvalues headers_;

    std::string content_;

    sys::error_code ec_;
};

const std::string& get(keyvalues const &l, const std::string& name)
{
    keyvalues::iterator it = l.find(name);
    if (it == l.end())
        THROW_EX(EN_HTTPParam_NotFound);
    return it->second;
}

const std::string& get(keyvalues const &l, const std::string& name, const std::string& defa)
{
    keyvalues::iterator it = l.find(name);
    if (it == l.end())
        return defa;
    return it->second;
}

bool exist(keyvalues const &l, const std::string& name)
{
    return (l.end() != l.find(name));
}

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
        bool empty = write_ls_.empty();
        write_ls_.push_back(std::make_pair(buffers, handler));
        if (empty)
        {
            std::pair<boost::asio::const_buffers_1
                , boost::function<void (sys::error_code const&)> >& p = write_ls_.front();
            boost::asio::async_write(socket()
                    , p.first
                    , boost::bind(&connection::handle_write, this
                        , p.second, placeholders::error/*, placeholders::bytes_transferred*/));
        }
    }

private:
    void handle_write(boost::function<void (sys::error_code const&)> fn, sys::error_code const& ec/*, std::size_t bytes*/)
    {
        fn(ec);

        write_ls_.pop_front();
        if (!write_ls_.empty())
        {
            std::pair<boost::asio::const_buffers_1
                , boost::function<void (sys::error_code const&)> >& p = write_ls_.front();
            boost::asio::async_write(socket()
                    , p.first
                    , boost::bind(&connection::handle_write, this
                        , p.second, placeholders::error/*, placeholders::bytes_transferred*/));
        }
    }

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

        message_.content(rsbuf_);

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

    std::list<std::pair<boost::asio::const_buffers_1, boost::function<void (sys::error_code const&)> > > write_ls_;

    void handle_error(sys::error_code const & ec)
    {
        _request(message(ec));

        LOG_I << ec;
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

#define PERM_PASSWORD "abc123"
std::string getpwd( std::size_t max_length, boost::asio::ssl::context::password_purpose purpose) { return PERM_PASSWORD; }

std::ostream& operator<<(std::ostream& out, const keyvalues& kv)
{
    for (keyvalues::iterator i = kv.begin(); i != kv.end(); ++i)
        out << i->first << ":" << i->second << "#";
    return out;
}

std::ostream& operator<<(std::ostream& out, const request& req)
{
    return out << req.method() << " " << req.path() << "|" << req.params()
        << "|" << req.headers();
}

struct apple_push
{
    typedef apple_push this_type;

    struct push_data
    {
        push_data(connection &c) { c_ = &c; }

        bool connected() const { return c_; }
        void disconnect() { c_ = 0; }

        void relay_back(sys::error_code const & ec)
        {
            replybuf_ = ec ? "FAIL" : "OK";

            data_.pop_front();
            c_->send(boost::asio::buffer(replybuf_)
                    , boost::bind(&push_data::handle_reply, this, placeholders::error));
        }

        void handle_reply(sys::error_code const & ec)
        {
            LOG_I << ec <<" "<< ec.message();
        }

        bool empty() const { return data_.empty(); }
        std::string const & data() const { return data_.front(); }

        void append(const std::string& m) { data_.push_back( m ); }

        connection* c_;
        std::list<std::string> data_;
        std::string replybuf_;
    };

    struct stage {
        enum type {
            idle = 0,
            halt = 1,
            connecting,
            handshaking,
            writing,
        };
    };

    typedef boost::shared_ptr<push_data> push_data_ptr;

    typedef std::list<push_data_ptr> bufs_type;

    apple_push(boost::asio::io_service & io_service, tcp::endpoint const & ep)
        : context_(boost::asio::ssl::context::sslv3)
        , socket_(io_service, context_)
        , endpoint_(ep)
    {
        context_.set_password_callback(getpwd);
        context_.use_certificate_file(RSA_CLIENT_CERT, boost::asio::ssl::context::pem);
        context_.use_private_key_file(RSA_CLIENT_KEY, boost::asio::ssl::context::pem);

        // connect();
    }

    connection::message_handler message(connection::message const& m, connection& c)
    {
        push_data_ptr ptr(new push_data(c));
        bufs_.push_back(ptr);
        pmessage(ptr, m);
        return boost::bind(&apple_push::pmessage, this, ptr, _1);
    }

private:
    void pmessage(push_data_ptr ptr, connection::message const & m)
    {
        BOOST_ASSERT(ptr);
        if (m.error())
        {
            ptr->disconnect();
            return;
        }

        ptr->append( on_REQUEST(m) );

        if (stage_ == stage::idle)
        {
            write_msg();
        }
        // else if (stage_ < stage::ready) { connect(); }
    }

    template <typename I> static char * pint(char *p, I x)
    {
        I i;
        switch (sizeof(I)) {
        case sizeof(uint32_t): i = htonl(x); break;
        case sizeof(uint16_t): i = htons(x); break;
        default: i = x; break;
        }
        memcpy(p, (void*)&i, sizeof(I));
        return p+sizeof(I);
    }
    static char * pdata(char *p, uint16_t len, const char *data)
    {
        uint16_t i = htons(len);
        memcpy(p, (void*)&i, sizeof(i));
        memcpy(p+2, data, len);
        return p+2+len;
    }

    std::string apack(std::string const & tok, std::string const & aps)
    {
        BOOST_ASSERT(tok.length() == 32);

        uint32_t len = sizeof(uint8_t) + 2*sizeof(uint32_t)+ 2*sizeof(uint16_t) + tok.length() + aps.length();
        std::string buf(len, 0);
        pdata(pdata(pint(pint(pint(const_cast<char*>(buf.data())
                , uint8_t(1)) // command
                , uint32_t(1234)) // /* provider preference ordered ID */
                , uint32_t(time(NULL)+86400)) // /* expiry date network order */
                , tok.length(), tok.data()) // binary 32bytes device token
                , aps.length(), aps.data())
                ;
        return buf;

        // char *binaryMessagePt = const_cast<char*>(buf.data());

        // uint8_t command = 1;
        // /* aps format is, |COMMAND|ID|EXPIRY|TOKENLEN|TOKEN|PAYLOADLEN|PAYLOAD| */
        // uint32_t whicheverOrderIWantToGetBackInAErrorResponse_ID = 1234;
        // // expire aps if not delivered in 1 day
        // uint32_t networkOrderExpiryEpochUTC = htonl(time(NULL)+86400);
        // uint16_t networkOrderTokenLength = htons(DEVTOK_LEN);
        // uint16_t networkOrderPayloadLength = htons(aps.length());

        // /* command */
        // *binaryMessagePt++ = command;
        // /* provider preference ordered ID */
        // memcpy(binaryMessagePt, &whicheverOrderIWantToGetBackInAErrorResponse_ID, sizeof(uint32_t));
        // binaryMessagePt += sizeof(uint32_t);
        // /* expiry date network order */
        // memcpy(binaryMessagePt, &networkOrderExpiryEpochUTC, sizeof(uint32_t));
        // binaryMessagePt += sizeof(uint32_t);
        // /* token length network order */
        // memcpy(binaryMessagePt, &networkOrderTokenLength, sizeof(uint16_t));
        // binaryMessagePt += sizeof(uint16_t);
        // /* device token */
        // memcpy(binaryMessagePt, deviceTokenBinary, DEVTOK_LEN);
        // binaryMessagePt += DEVTOK_LEN;
        // /* payload length network order */
        // memcpy(binaryMessagePt, &networkOrderPayloadLength, sizeof(uint16_t));
        // binaryMessagePt += sizeof(uint16_t);
        // /* payload */
        // memcpy(binaryMessagePt, aps.data(), aps.length());
        // binaryMessagePt += aps.length();
    }

    static std::string unhex(const std::string & hs)
    {
        std::string buf;
        const char* s = hs.data();
        for (unsigned int x = 0; x+1 < hs.length(); x += 2)
        {
            char h = s[x];
            char l = s[x+1];
            h -= (h <= '9' ? '0': 'a');
            l -= (l <= '9' ? '0': 'a');
            buf.push_back( char((h << 4) | l) );
        }
        return buf;
    }

    std::string on_REQUEST(connection::message const & m)
    {
        LOG_I << m;
        LOG_I << m.content();
        std::string const & tok = get(m.params(), "devtok");
        std::string const & msg = get(m.params(), "message");
        std::string aps = json::encode(json::object()("aps", json::object()("alert",msg)));
        LOG_I << aps;
        return apack(unhex(tok), aps);
    }

    void connect()
    {
        stage_ = stage::connecting;

        sys::error_code ec;
        socket_.lowest_layer().close(ec);
        socket_.lowest_layer().open(boost::asio::ip::tcp::v4());
        socket_.lowest_layer().async_connect(endpoint_, boost::bind(&this_type::handle_connect, this, placeholders::error));
        LOG_I << "connecting ... " << ec;
    }

    void handle_connect(sys::error_code const & ec)
    {
        if (ec)
        {
            return handle_error(ec);
        }

        stage_ = stage::handshaking;
        socket_.async_handshake(boost::asio::ssl::stream_base::client,
                boost::bind(&this_type::handle_handshake, this, boost::asio::placeholders::error));
    }

    void handle_handshake(const boost::system::error_code& ec)
    {
        if (ec)
        {
            std::cout << "Handshake failed: " << ec.message() << "\n";
            return;
        }

        boost::asio::async_read(socket_, rdbuf_
                , boost::asio::transfer_at_least(32)
                , boost::bind(&this_type::handle_read, this, placeholders::error));

        write_msg();

        // request_ = "{ \"aps\" : { \"alert\" : \"This is the alert text\", \"badge\" : 1, \"sound\" : \"default\" } }";
        // std::cout << request_;
        // std::cout << "Enter message: ";
        // std::cin.getline(request_, max_length);
        // size_t request_length = request_.size();
        //
        // boost::asio::async_write(socket_,
        //         boost::asio::buffer(request_.data(), request_length),
        //         boost::bind(&client::handle_write, this,
        //             boost::asio::placeholders::error,
        //             boost::asio::placeholders::bytes_transferred));
    }

    void handle_read(sys::error_code const & ec)
    {
        if (ec)
        {
            LOG_I << ec;
            connect();
            return;
        }

        LOG_I << &rdbuf_;
        boost::asio::async_read(socket_, rdbuf_
                , boost::asio::transfer_at_least(32)
                , boost::bind(&this_type::handle_read, this, placeholders::error));
    }

    void write_msg()
    {
        if (stage_ != stage::idle || bufs_.empty())
            return;

        for (bufs_type::iterator i = bufs_.begin(); i != bufs_.end(); )
        {
            if ((*i)->connected())
                ++i;
            else
                i = bufs_.erase(i);
        }

        for (bufs_type::iterator i = bufs_.begin(); i != bufs_.end(); ++i)
            if (!(*i)->empty())
            {
                if (i != bufs_.begin())
                    bufs_.splice(bufs_.begin(), bufs_, i);
                break;
            }

        if (bufs_.front()->empty())
            return;

        stage_ = stage::writing;
        boost::asio::async_write(socket_
                , boost::asio::buffer(bufs_.front()->data())
                , boost::bind(&this_type::handle_write, this, placeholders::error));
    }

    void handle_write(sys::error_code const & ec)
    {
        LOG_I << ec <<" "<< ec.message();
        BOOST_ASSERT (!bufs_.empty());

        stage_ = stage::idle;

        if (bufs_.empty())
        {
            return;
        }

        push_data_ptr & ptr = bufs_.front();

        if (!ptr->connected())
        {
            bufs_.pop_front();
            write_msg();
            return;
        }

        if (ec)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;
        }

        ptr->relay_back(ec);

        bufs_.splice(bufs_.end(), bufs_, bufs_.begin());
        write_msg();
    }

    void handle_error(sys::error_code const & ec)
    {
        LOG_I << ec <<" "<< ec.message();
        connect();
    }

private:
    stage::type stage_;

    boost::asio::ssl::context context_;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;

    tcp::endpoint endpoint_;
    bufs_type bufs_;

    boost::asio::streambuf rdbuf_;
};

int main(int argc, char* argv[])
{
    std::string addr = "127.0.0.1";
    std::string port = "9991";

    try
    {
        boost::asio::io_service io_service;

        apple_push ap(io_service, resolve(io_service, APPLE_HOST, APPLE_PORT));
        relay_server<connection> s(io_service);

        s.start(resolve(io_service, addr, port), boost::bind(&apple_push::message, &ap, _1, _2));

        io_service.run();
    }
    catch (std::exception& e)
    {
        LOG_E << "exception: " << e.what();
    }

    return 0;
}


