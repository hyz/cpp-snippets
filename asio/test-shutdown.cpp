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

class posix_chat_client
{
char read_buf_[256];
public:
  posix_chat_client(boost::asio::io_service& io_service,
      tcp::resolver::iterator endpoint_iterator)
    : socket_(io_service),
      input_(io_service, ::dup(STDIN_FILENO)),
      output_(io_service, ::dup(STDOUT_FILENO))
  {
    boost::asio::async_connect(socket_, endpoint_iterator,
        boost::bind(&posix_chat_client::handle_connect, this, boost::asio::placeholders::error));
  }

private:
  void handle_connect(const boost::system::error_code& error)
  {
    if (!error)
    {
      socket_.async_read_some(
          boost::asio::buffer(read_buf_, sizeof(read_buf_)),
          boost::bind(&posix_chat_client::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

      socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send);
    }
    else std::clog << error << __LINE__;
  }

  void handle_read(const boost::system::error_code& error, size_t bytes)
  {
    if (!error)
    {
      fwrite(read_buf_, bytes, 1, stdout);

      socket_.async_read_some(
          boost::asio::buffer(read_buf_, sizeof(read_buf_)),
          boost::bind(&posix_chat_client::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
    }
    else std::clog << error << __LINE__;
  }

  void close()
  {
    socket_.close();
    input_.close();
    output_.close();
  }

private:
  tcp::socket socket_;
  posix::stream_descriptor input_;
  posix::stream_descriptor output_;
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 3)
    {
      std::cerr << "Usage: posix_chat_client <host> <port>\n";
      return 1;
    }

    boost::asio::io_service io_service;

    tcp::resolver resolver(io_service);
    tcp::resolver::query query(argv[1], argv[2]);
    tcp::resolver::iterator iterator = resolver.resolve(query);

    posix_chat_client c(io_service, iterator);

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
