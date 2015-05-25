//
// posix_chat_client.cpp
// ~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

#if defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)

using boost::asio::ip::tcp;
namespace posix = boost::asio::posix;
namespace placeholders = boost::asio::placeholders;

class posix_chat_client
{
  char input_buf_[256];
  char output_buf_[256];
public:
  posix_chat_client(boost::asio::io_service& io_service, std::string const & named_sock)
    : socket_(io_service),
      input_(io_service, ::dup(STDIN_FILENO))
  {
    socket_.async_connect(
            boost::asio::local::stream_protocol::endpoint(named_sock),
        boost::bind(&posix_chat_client::handle_connect, this, placeholders::error));
  }

private:
  void handle_connect(const boost::system::error_code& error)
  {
    if (!error)
    {
      socket_.async_read_some( boost::asio::buffer(output_buf_, sizeof(output_buf_)),
          boost::bind(&posix_chat_client::handle_write_stdout, this, placeholders::error, placeholders::bytes_transferred));

      handle_write_sock(boost::system::error_code());
    }
  }

  void handle_write_stdout(const boost::system::error_code& error, size_t bytes)
  {
    if (!error)
    {
      fwrite(output_buf_, bytes, 1, stdout);

      socket_.async_read_some( boost::asio::buffer(output_buf_, sizeof(output_buf_)),
          boost::bind(&posix_chat_client::handle_write_stdout, this, placeholders::error, placeholders::bytes_transferred));
    }
    else
    {
      close();
    }
  }

  void handle_write_sock(boost::system::error_code const & ec)
  {
    if (!ec)
    {
      input_.async_read_some(boost::asio::buffer(input_buf_, sizeof(input_buf_)),
          boost::bind(&posix_chat_client::handle_read_input, this, placeholders::error, placeholders::bytes_transferred));
    }
  }

  void handle_read_input(const boost::system::error_code& error, std::size_t length)
  {
    if (!error)
    {
      boost::asio::async_write(socket_
          , boost::asio::buffer(input_buf_, length),
          boost::bind(&posix_chat_client::handle_write_sock, this, placeholders::error)
          );
    }
    if (error == boost::asio::error::eof)
    {
      boost::system::error_code ec;
      socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
      std::clog << __LINE__ << "eof\n";
    }
  }

  void close()
  {
    socket_.close();
    input_.close();
  }

private:
  boost::asio::local::stream_protocol::socket socket_;
  posix::stream_descriptor input_;
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: posix_chat_client <named-socket-file>\n";
      return 1;
    }

    boost::asio::io_service io_service;

    //tcp::resolver resolver(io_service);
    //tcp::resolver::query query(argv[1], argv[2]);
    //tcp::resolver::iterator iterator = resolver.resolve(query);

    posix_chat_client c(io_service, argv[1]);

    io_service.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

#else // defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
int main() {}
#endif // defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
