#include <cstdlib>
#include <deque>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/log/common.hpp>
// #include "chat_message.hpp"

using boost::asio::ip::tcp;
namespace asio = boost::asio;

typedef std::deque<std::string> message_queue;

// static std::string mac_ = "00:11:22";
static std::string spot_in_; // = "KK1007A";
static std::string spot_pk_; // = "KK1007A";
static std::string host_ = "127.0.0.1";
static std::string port_ = "9900";

namespace json {
    using boost::property_tree::ptree;

    struct Int
    {
        typedef std::string internal_type;
        typedef int external_type;
        // boost::optional<external_type> get_value(const internal_type &v) { return boost::lexical_cast<extrenal_type>(v); }
        boost::optional<internal_type> put_value(const external_type &v) { return boost::lexical_cast<internal_type>(v); }
    };

    std::string encode(boost::property_tree::ptree const & pt)
    {
      std::ostringstream oss;
      boost::property_tree::json_parser::write_json(oss, pt, false);
      return oss.str();
    }

    ptree decode(const char * buf, const char * endbuf)
    {
      ptree pt;
      std::istringstream iss(std::string(buf, endbuf));
      boost::property_tree::json_parser::read_json(iss, pt);
      return pt;
    }
}

class chat_client
{
public:
  chat_client(asio::io_service& io_service
      , tcp::resolver::iterator endpoint_iterator
      , std::string const & token
      )
    : io_service_(io_service)
      , socket_(io_service)
      , deadline_(io_service)
  {
    do_connect(endpoint_iterator);
    token_ = token;
    deadline_.expires_from_now(boost::posix_time::seconds(0));
    keep_alive();
  }

  void close(boost::system::error_code ec)
  {
    io_service_.post([this]() {
                socket_.close();
                deadline_.cancel();
            });
    std::cout << ec << " close\n";
  }

  bool is_open() const { return socket_.is_open(); }

private:
  void keep_alive()
  {
    bool expired = (deadline_.expires_at() <= asio::deadline_timer::traits_type::now());
    if (expired)
    {
        // deadline_.async_wait([this]() { this->keep_alive(); });
        deadline_.expires_from_now(boost::posix_time::seconds(33));
        deadline_.async_wait(std::bind(&chat_client::keep_alive, this));

        json::ptree head, body;
        head.put("method", "hello");
        head.put("token", token_);

        if (!spot_in_.empty())
        {
            body.put("spotsid",spot_in_);
        }
        if (!spot_pk_.empty())
        {
            body.put("usingsid",spot_pk_);
        }

        std::string heads = json::encode(head);
        std::string bodys = json::encode(body);
        decltype(read_size_) u;
        u.h[0] = htons(static_cast<short>(heads.size()));
        u.h[1] = htons(static_cast<short>(bodys.size()));

        this->write(std::string(&u.s[0],sizeof(u)) + heads + bodys);
    }
  }

  void do_connect(tcp::resolver::iterator endpoint_iterator)
  {
    asio::async_connect(socket_, endpoint_iterator,
        [this](boost::system::error_code ec, tcp::resolver::iterator)
        {
          if (!ec)
          {
            do_read_size();
          }
        });
  }

  void do_read_size()
  {
    // std::cout << "do read size " << sizeof(read_size_) << "\n";
    asio::async_read(socket_, asio::buffer(&read_size_.s[0], sizeof(read_size_)),
        [this](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            read_size_.h[0] = ntohs(read_size_.h[0]);
            read_size_.h[1] = ntohs(read_size_.h[1]);
            read_msg_.resize( read_size_.h[0] + read_size_.h[1] );
            do_read_data();
          }
          else
          {
            close(ec); // socket_.close();
          }
        });
  }

  void do_read_data()
  {
    asio::async_read(socket_, asio::buffer(const_cast<char*>(read_msg_.data()), read_msg_.size()),
        [this](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            std::cout << ">>> "<< read_msg_ << "\n";

            json::ptree head = json::decode(&read_msg_[0], &read_msg_[read_size_.h[0]]);
            ack( head.get<int>("sequence") );

            if (!boost::starts_with(head.get<std::string>("method"), "data/"))
            {
              json::ptree body = json::decode(&read_msg_[read_size_.h[0]], &read_msg_[read_msg_.size()]);
              process(head, body);
            }

            do_read_size();
          }
          else
          {
            close(ec); // socket_.close();
          }
        });
  }

  void process(json::ptree const & head, const json::ptree & body)
  {
      const std::string & meth = head.get<std::string>("method");
      std::cout <<"=== "<< meth << "\n";

      if (meth == "chat/in-spot")
      {
          spot_in_ = body.get<std::string>("content");
      }
      else if (meth == "chat/out-spot")
      {
          spot_in_ = std::string();
      }
  }

  void ack(int seqn)
  {
    json::ptree head;
    head.put("token", token_);
    head.put("method", "ack");
    head.put("sequence", seqn);

    std::string heads = boost::replace_all_regex_copy(json::encode(head)
        , boost::regex("\"sequence\":\"([0-9]+?)\""), std::string("\"sequence\":$1"));

    time_t t = time(0);
    std::cout << "<<< "<< heads <<" "<< ctime(&t) << std::endl;

    decltype(read_size_) u;
    u.h[0] = htons(static_cast<short>(heads.size()));
    u.h[1] = htons(2);
    this->write(std::string(&u.s[0],sizeof(u)) + heads + "{}");
  }

  void write(const std::string& msg)
  {
    io_service_.post(
        [this, msg]()
        {
        // std::cout << msg << "#POST\n";
          bool write_in_progress = !write_msgs_.empty();
          write_msgs_.push_back(msg);
          if (!write_in_progress)
          {
            do_write();
          }
        });
  }

  void do_write()
  {
    // std::cout << "do write " << write_msgs_.front() << "\n";
    asio::async_write(socket_,
        asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
        [this](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            // std::cout << "<<< "<< write_msgs_.front() <<" "<< ctime(&t) << std::endl;
            write_msgs_.pop_front();
            if (!write_msgs_.empty())
            {
              do_write();
            }
          }
          else
          {
            close(ec); // socket_.close();
          }
        });
  }

private:
  asio::io_service& io_service_;
  tcp::socket socket_;
  std::string token_;
  asio::deadline_timer deadline_;


  union { uint16_t h[2]; char s[4]; uint32_t len; } read_size_;
  std::string read_msg_;

  message_queue write_msgs_;
};

#include <boost/program_options.hpp>
namespace po = boost::program_options;

std::string args(int ac, char * const av[])
{
    std::string token;

    po::options_description opt_desc("Options");
    opt_desc.add_options()
        ("help", "display this help and exit")
        ("host,h", po::value<std::string>(&host_)->default_value(host_), "host to connect")
        ("port,p", po::value<std::string>(&port_)->default_value(port_), "port to connect")
        // ("mac", po::value<std::string>(&mac_)->default_value(mac_), "the macaddress")
        ("spot", po::value<std::string>(&spot_pk_)->default_value(spot_pk_), "spot group-id")
        ("token", po::value<std::string>(&token)->required(), "client login-token")
        ;
    po::positional_options_description pos_desc;
    pos_desc.add("token", 1);

    po::variables_map vm;
    po::store(po::command_line_parser(ac, av)
            .options(opt_desc)
            .positional(pos_desc)
            .run(), vm);
    if (vm.count("help"))
    {
        std::cout << "Usage: a.out [options] <token>\n"
            << opt_desc;
        exit(0);
    }

    po::notify(vm);

    return token;
}

int main(int ac, char* const av[])
{
  try
  {
    std::string token = args(ac, av);

    asio::io_service io_service;

    tcp::resolver resolver(io_service);
    auto endpoint_iterator = resolver.resolve({ host_, port_ });

    chat_client c(io_service, endpoint_iterator, token);
    // c.keep_alive();

    // std::thread t([&io_service](){ io_service.run(); });
    // while (c.is_open())
    //{
        // c.keep_alive();
        // std::this_thread::sleep_for(std::chrono::seconds(30));
    //}

    // char line[chat_message::max_body_length + 1];
    // while (std::cin.getline(line, chat_message::max_body_length + 1))
    // {
    //   chat_message msg;
    //   msg.body_length(std::strlen(line));
    //   std::memcpy(msg.body(), line, msg.body_length());
    //   msg.encode_header();
    //   c.write(msg);
    // }

    // boost::system::error_code ec;
    // c.close(ec);
    //
    // t.join();

    io_service.run();
    std::cout << "bye.\n";
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

