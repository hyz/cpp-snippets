//
// async_client.cpp
// ~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2012 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

using boost::asio::ip::tcp;
namespace asio = boost::asio;

struct client
{
    client(asio::io_service& io_service,
            const std::string& server, const std::string& path)
        : resolver_(io_service),
        socket_(io_service)
    {
        body_size_ = 0;
        isfin_ = false;

        // Form the request. We specify the "Connection: close" header so that the
        // server will close the socket after transmitting the response. This will
        // allow us to treat all data up until the EOF as the content.
        std::ostream request_stream(&request_);
        request_stream << "GET " << path << " HTTP/1.0\r\n";
        request_stream << "Host: " << server << "\r\n";
        request_stream << "Accept: */*\r\n";
        request_stream << "Connection: close\r\n\r\n";

        // Start an asynchronous resolve to translate the server and service names
        // into a list of endpoints.
        tcp::resolver::query query(server, "http");
        resolver_.async_resolve(query,
                boost::bind(&client::handle_resolve, this,
                    asio::placeholders::error,
                    asio::placeholders::iterator));
    }

    private:
    void handle_resolve(const boost::system::error_code& err,
            tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            // Attempt a connection to each endpoint in the list until we
            // successfully establish a connection.
            asio::async_connect(socket_, endpoint_iterator,
                    boost::bind(&client::handle_connect, this,
                        asio::placeholders::error));
        }
        else
        {
            std::cout << "Error: " << err.message() << "\n";
        }
    }

    void handle_connect(const boost::system::error_code& err)
    {
        if (!err)
        {
            // The connection was successful. Send the request.
            asio::async_write(socket_, request_,
                    boost::bind(&client::handle_write_request, this,
                        asio::placeholders::error));
        }
        else
        {
            std::cout << "Error: " << err.message() << "\n";
        }
    }

    void handle_write_request(const boost::system::error_code& err)
    {
        if (err)
        {
            return this->handle_error(err, 9);
        }

        // Read the response status line. The response_ streambuf will
        // automatically grow to accommodate the entire line. The growth may be
        // limited by passing a maximum size to the streambuf constructor.
        asio::async_read_until(socket_, response_, "\r\n",
                boost::bind(&client::handle_read_status_line, this,
                    asio::placeholders::error));
    }

    void handle_read_status_line(const boost::system::error_code& err)
    {
        if (err)
        {
            if (err == asio::error::eof)
            {
                ;
            }
            return this->handle_error(err, 1);
        }

        // Check that response is OK.
        std::istream response_stream(&response_);
        std::string http_version;
        response_stream >> http_version;
        unsigned int status_code;
        response_stream >> status_code;
        std::string status_message;
        std::getline(response_stream, status_message);

        if (!response_stream || http_version.substr(0, 5) != "HTTP/")
        {
            std::cout << "Invalid response\n";
            return this->handle_error(err, 1);
        }
        if (status_code != 200)
        {
            std::cout << "Response returned with status code " << status_code << "\n";
            return this->handle_error(err, 1);
        }

        // Read the response headers, which are terminated by a blank line.
        asio::async_read_until(socket_, response_, "\r\n\r\n",
                boost::bind(&client::handle_read_headers, this,
                    asio::placeholders::error));
    }

    void handle_read_headers(const boost::system::error_code& err)
    {
        if (err)
        {
            return this->handle_error(err, 2);
        }

        // Process the response headers.
        std::istream response_stream(&response_);
        std::string line;
        while (std::getline(response_stream, line) && line != "\r")
        {
            std::string k;

            k = "Cookies";
            if (k.length() < line.length() && std::equal(k.begin(), k.end(), line.begin()))
            {
                boost::regex e("^Cookies:.*[\\s;]ck=([^ ;\t\r\n]+)\\s*$");
                boost::smatch res;
                if (boost::regex_match(line, res, e))
                {
                    ckval_.assign(res[1].first, res[1].second);
                }
            }

            k = "Content-Length";
            if (k.length() < line.length() && std::equal(k.begin(), k.end(), line.begin()))
            {
                boost::regex e("^Content-[Ll]ength:\\s*(\\d+)\\s*$");
                boost::smatch res;
                if (boost::regex_match(line, res, e))
                {
                    body_size_ = boost::lexical_cast<int>(res[1]);
                }
            }

            std::cout << line << "\n";
        }
        std::cout << "\n";

        if (body_size_ <= 0 || body_size_ > 1024*512)
        {
            return this->handle_error(err, 2);
        }
        //(find/create object)

        // Write whatever content we already have to output.
        if (response_.size() >= body_size_)
        {
            return handle_read_content(err);
        }

        // Start reading remaining data until EOF.
        asio::async_read(socket_, response_,
                asio::transfer_at_least(body_size_),
                boost::bind(&client::handle_read_content, this,
                    asio::placeholders::error));
    }

    void handle_read_content(const boost::system::error_code& err)
    {
        if (err)
        {
            return this->handle_error(err, 3);
        }

        //(find/create object)
        //
        std::cout << &response_;
        ///
        //
        //

        response_.consume(response_.size());

        // Continue
        asio::async_read_until(socket_, response_, "\r\n",
                boost::bind(&client::handle_read_status_line, this,
                    asio::placeholders::error));
    }

    void handle_error(const boost::system::error_code& err, int w)
    {
        std::cout << "Error: " << err.message() << "\n";

        if (err == asio::error::eof)
        {
        }
    }

    tcp::resolver resolver_;
    tcp::socket socket_;
    asio::streambuf request_;
    asio::streambuf response_;

    unsigned int body_size_;
    std::string ckval_;

    bool isfin_;
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 3)
    {
      std::cout << "Usage: async_client <server> <path>\n";
      std::cout << "Example:\n";
      std::cout << "  async_client www.boost.org /LICENSE_1_0.txt\n";
      return 1;
    }

    asio::io_service io_service;
    client c(io_service, argv[1], argv[2]);
    io_service.run();
  }
  catch (std::exception& e)
  {
    std::cout << "Exception: " << e.what() << "\n";
  }

  return 0;
}
