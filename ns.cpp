#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/regex.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <boost/format.hpp>
using boost::format;

using boost::bind;
using boost::function;
using boost::shared_ptr;

using boost::asio::ip::tcp;
namespace asio = boost::asio;

typedef boost::shared_ptr<tcp::socket> socket_ptr;

struct http_reader_impl : boost::enable_shared_from_this<http_reader_impl> //, boost::noncopyable
{
    http_reader_impl(socket_ptr soc
            , function<void (socket_ptr, const http_request&)> rsp)
        : socket_(soc), rsp_(rsp)
    {
    }

    void start()
    {
        asio::async_read_until(*socket_, rsbuf_, "\r\n",
                bind(&http_reader_impl::handle_method_line, shared_from_this(), asio::placeholders::error));
    }

private:
    typedef boost::shared_ptr<http_reader_impl> this_shptr;

    socket_ptr socket_;
    asio::streambuf rsbuf_;
    http_request req_;

    function<void (socket_ptr, const http_request&)> rsp_;

private:
    void handle_method_line(const boost::system::error_code& err)
    {
        if (err)
        {
            if (err == asio::error::eof)
            {
                ;
            }
            return _error(err, 1);
        }

        req_ = http_request();

        // Check that  is OK.
        std::istream ins(&rsbuf_);
        ins >> req_.method;
        boost::trim(req_.method);
        if (req_.method.empty())
        {
            asio::async_read_until(*socket_, rsbuf_, "\r\n",
                    bind(&http_reader_impl::handle_method_line, shared_from_this(), asio::placeholders::error)
                    );
            return;
        }

        ins >> req_.path >> req_.version;
        std::string tmp;
        std::getline(ins, tmp);

        std::cout << req_.method << " " << req_.path << " " << req_.version << "\n";

        asio::async_read_until(*socket_, rsbuf_, "\r\n\r\n",
                bind(&http_reader_impl::handle_headers, shared_from_this(), asio::placeholders::error));
    }

    void handle_headers(const boost::system::error_code& err)
    {
        if (err)
        {
            return _error(err, 2);
        }

        asio::streambuf& sbuf = rsbuf_;
        std::istream ins(&sbuf);
        std::string line;
        boost::regex expr("^\\s*([\\w\\-]+)\\s*:\\s*(.*?)\\s*$");

        while (std::getline(ins, line) && line != "\r" && !line.empty())
        {
            boost::smatch res;
            if (boost::regex_match(line, res, expr))
            {
                std::string k(res[1].first, res[1].second);
                std::string val(res[2].first, res[2].second);
                req_.headers[k] = val;

                if (k == "Content-Length" || k == "Content-length")
                {
                    req_.cont.resize(boost::lexical_cast<int>(val));
                }
            }

            std::cout << line << "\n";
        }

        std::cout << format("content len %d - %d\n") % req_.cont.size() % sbuf.size()
            ;

        asio::async_read(*socket_ , sbuf
                , asio::transfer_at_least(req_.cont.size())
                , bind(&http_reader_impl::handle_content, shared_from_this(), asio::placeholders::error));
    }

    void handle_content(const boost::system::error_code& err)
    {
        if (err)
        {
            return _error(err, 3);
        }

        rsbuf_.sgetn(&req_.cont[0], req_.cont.size());

        rsp_(socket_, req_);

        // Next http_request
        asio::async_read_until(*socket_, rsbuf_, "\r\n",
                bind(&http_reader_impl::handle_method_line, shared_from_this(), asio::placeholders::error));
    }

    static void _error(const boost::system::error_code& err, int w)
    {
        std::cout << "Error: " << err.message() << "\n";

        if (err == asio::error::eof)
        {
        }
    }
};

struct http_reader
{
    typedef http_reader_impl worker_t;

    http_reader(function<void (socket_ptr, const http_request&)> fn) : fn_(fn) {}

    void accept(socket_ptr soc)
    {
        shared_ptr<worker_t> wk(new worker_t(soc, fn_));
        wk->start();
    }

    function<void (socket_ptr, const http_request&)> fn_;
};

struct socket_acceptor
{
    socket_acceptor(asio::io_service& io_service, const char* addr, const char* port, function<void (socket_ptr)> fn);

    void start()
    {
        acceptor_.listen();

        socket_.reset(new tcp::socket(acceptor_.get_io_service()));
        acceptor_.async_accept(*socket_,
                bind(&socket_acceptor::handle_accept, this, asio::placeholders::error)
                );
    }

private:
    tcp::acceptor acceptor_;
    shared_ptr<tcp::socket> socket_;

    function<void (socket_ptr)> fn_;

private:
    void handle_accept(const boost::system::error_code& e)
    {
        if (e)
        {
            return _error(e);
        }

        std::cout << "accepted " << socket_->local_endpoint() << "%" << socket_->remote_endpoint() << "\n";

        fn_(socket_);

        socket_.reset(new tcp::socket(acceptor_.get_io_service()));
        acceptor_.async_accept(*socket_,
                bind(&socket_acceptor::handle_accept, this, asio::placeholders::error));
    }

    void _error(const boost::system::error_code& err)
    {
        std::cout << "acceptor error: " << err.message() << "\n";
    }
};

socket_acceptor::socket_acceptor(asio::io_service& io_service
        , const char* addr, const char* port
        , function<void (socket_ptr)> fn)
    : acceptor_(io_service)
    , fn_(fn)
{
    asio::ip::tcp::resolver resolver(acceptor_.get_io_service());
    asio::ip::tcp::resolver::query query(addr, port);
    asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
}

// void sigexit(asio::io_service *io_service) { io_service->stop(); }

void func_socket_ready(socket_ptr socket)
{
}

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cout << "Usage: ...\n";
            return 1;
        }

        asio::io_service io_service;

        /// The signal_set is used to register for process termination notifications.
        asio::signal_set sigstop(io_service);
        asio::signal_set sigw(io_service);

        sigstop.add(SIGINT);
        sigstop.add(SIGTERM);
#if defined(SIGQUIT)
        sigstop.add(SIGQUIT);
#endif // defined(SIGQUIT)
        sigstop.async_wait(bind(&asio::io_service::stop, &io_service));

        sender sender;
        http_chat responser(bind(&sender::get_send_fn, &sender, _1));
        http_reader reader(bind(&http_chat::query, &responser, _1, _2));
        socket_acceptor acceptor(io_service, "0", argv[1], bind(&http_reader::accept, &reader, _1));

        acceptor.start();

        io_service.run();
        std::cout << "bye.\n";
    }
    catch (std::exception& e)
    {
        std::cout << "Exception: " << e.what() << "\n";
    }

    return 0;
}

