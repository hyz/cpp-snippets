#include <cstdlib>
#include <deque>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <boost/container/vector.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/format.hpp>

// #include <boost/log/trivial.hpp>
#define BOOST_LOG_TRIVIAL(trace) (std::clog)

using boost::asio::ip::tcp;
namespace asio = boost::asio;
namespace placeholders = boost::asio::placeholders;

typedef std::deque<std::string> message_queue;

// static std::string mac_ = "00:11:22";
static std::string spot_in_; // = "KK1007A";
static std::string spot_pk_; // = "KK1007A";
static std::string host_ = "127.0.0.1";
static std::string port_ = "9000";
static const int Port_9900 = 9900;

namespace json {
    using boost::property_tree::ptree;

    std::string encode(boost::property_tree::ptree const & pt)
    {
      std::ostringstream oss;
      boost::property_tree::json_parser::write_json(oss, pt, false);
      return oss.str();
    }

    template <typename I> ptree decode(I begin, I end)
    {
      ptree pt;
      std::istringstream iss(std::string(begin, end));
      boost::property_tree::json_parser::read_json(iss, pt);
      return pt;
    }
}

class chat_client
{
    enum struct stage {
        init = 0
        , connecting
        , established
    };
    stage stage_;
    time_t stage_tp_;
    tcp::endpoint endp_;

public:
  chat_client(asio::io_service& io_service
          , tcp::resolver::iterator endpoint_iterator, std::pair<std::string,std::string> user)
    : io_service_(io_service)
      , socket_(io_service)
      , deadline_(io_service)
      , user_(user)
  {
    endp_ = *endpoint_iterator;
    stage_ = stage::init;
    stage_tp_ = 0;
    deadline_.expires_from_now(boost::posix_time::seconds(0));
    deadline_.async_wait(boost::bind(&chat_client::timed_loop, this, placeholders::error));
  }

  void close() // (boost::system::error_code ec)
  {
    io_service_.post([this]() {
                socket_.close();
                deadline_.cancel();
            });
    // std::cout << ec << " close\n";
  }

  bool is_open() const { return socket_.is_open(); }

private:
  struct http_t
  {
      http_t(asio::io_service & io_s, std::pair<std::string,std::string> user)
          : socket_(io_s)
          , user_(user)
      {}

      void start(tcp::endpoint const &endp, boost::function<void (std::string)> fn)
      {
          fn_ = fn;
          socket_.async_connect(endp, [this](boost::system::error_code ec) {
                  if (!ec)
                    sign_in();
                  });
          BOOST_LOG_TRIVIAL(trace) << "http start " << endp;
      }

      void sign_in()
      {
          bufs_ = str(boost::format("GET /login"
                      "?systemVersion=1.0&deviceType=3&channel=3"
                      "&type=3&phone=%1%&password=%2% HTTP/1.1\r\n"
                      "Host: moon.kklink.com:9000\r\n"
                      "Connection: close\r\n"
                      "\r\n") % user_.first % user_.second);
          asio::async_write(socket_, asio::buffer(bufs_),
                  [this](boost::system::error_code ec, size_t) {
                      bufs_.clear();
                      if (!ec)
                          read_response();
                  });
          BOOST_LOG_TRIVIAL(trace) << "http sign in " << user_.first;
      }

      void read_response()
      {
          socket_.async_read_some(asio::buffer(tmps_, sizeof(tmps_))
                  , [this](boost::system::error_code ec, size_t bytes) {
                    if (!ec) {
                      bufs_.insert(bufs_.end(), &tmps_[0], &tmps_[bytes]);
                      std::string tok;
                      boost::logic::tribool b = parse_response_data(tok);
                      if (b) {
          BOOST_LOG_TRIVIAL(trace) << "http resp ok " << tok;
                        fn_(tok);
                      } else if (boost::logic::indeterminate(b)) {
                          read_response();
                      }
                    }
                  });
      }

      boost::logic::tribool parse_response_data(std::string & token)
      {
          boost::iterator_range<std::string::iterator> eoh = boost::find_first(bufs_, "\r\n\r\n");
          if (boost::empty(eoh))
          {
              return boost::logic::indeterminate_keyword_t();
          }
          // LOG_I << boost::asio::const_buffer(bufs_.data(), eoh.end()-bufs_.begin());

          boost::iterator_range<std::string::iterator> cl = boost::ifind_first(bufs_, "content-Length");
          if (boost::empty(cl))
          {
              return false;
          }

          size_t clen = atoi(&cl.end()[2]);
          // LOG_I << "content-Length " << clen <<" "<< bufs_.size() - (eoh.end() - bufs_.begin());
          if (eoh.end() + clen > bufs_.end())
          {
              return boost::logic::indeterminate_keyword_t();
          }

          boost::iterator_range<std::string::iterator> body(eoh.end(), eoh.end() + clen);

          json::ptree js = json::decode(body.begin(), body.end());
          std::string tok;
          tok = js.get<std::string>("token", tok);
          if (tok.empty())
              return false;
          token = tok;

          return true;
      }

      tcp::socket socket_;
      boost::function<void (std::string)> fn_;
      std::pair<std::string,std::string> user_;
      std::string bufs_;
      char tmps_[64];
  };

  void timed_loop(boost::system::error_code ec)
  {
      if (ec == boost::asio::error::operation_aborted)
          return;

      switch (stage_) {
      case stage::init:
          if (stage_tp_ + 12 < time(0)) {
              stage_tp_ = time(0);
              http_ = std::make_shared<http_t>(io_service_, user_);
              http_->start(endp_, [this] (std::string tok) {
                        token_ = tok;
                        stage_ = stage::connecting;
                        stage_tp_ = time(0)-6;
                      });
          }
          break;
      case stage::connecting:
          if (stage_tp_ + 6 > time(0))
              break;
          stage_tp_ = time(0);
          socket_.close(ec);
          socket_.open(tcp::v4());
          {
          tcp::endpoint endp(endp_.address(), Port_9900);
          socket_.async_connect(endp, [this](boost::system::error_code ec) {
                if (!ec) {
                  stage_ = stage::established;
                  stage_tp_ = time(0);
                  do_read_size();
                  BOOST_LOG_TRIVIAL(trace) << "im connected";
                }
              });
          BOOST_LOG_TRIVIAL(trace) << "im connecting " << endp;
          }
          break;
      case stage::established:
          if (stage_tp_ <= time(0)) {
              hello();
              stage_tp_ = time(0) + 55;
          }
          break;
      default:
          return;
      }
      deadline_.expires_from_now(boost::posix_time::seconds(2));
      deadline_.async_wait(boost::bind(&chat_client::timed_loop, this, placeholders::error));
  }

  void do_socket_error(boost::system::error_code ec)
  {
    if (stage_ == stage::established)
    {
        stage_ = stage::connecting;
        stage_tp_ = time(0) - 5;
    }
  }

  void do_read_size()
  {
    // std::cout << "do read size " << sizeof(read_size_) << "\n";
    asio::async_read(socket_, asio::buffer(&read_size_.s[0], sizeof(read_size_)),
        [this](boost::system::error_code ec, std::size_t /*length*/) {
          if (!ec) {
            read_size_.h[0] = ntohs(read_size_.h[0]);
            read_size_.h[1] = ntohs(read_size_.h[1]);
            read_msg_.resize( read_size_.h[0] + read_size_.h[1] );
            do_read_data();
          } else {
            do_socket_error(ec);
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
            do_socket_error(ec);
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

  void hello()
  {
    json::ptree head, body;
    head.put("method", "hello");
    head.put("token", token_);

    if (!spot_in_.empty())
        body.put("spotsid",spot_in_);
    if (!spot_pk_.empty())
        body.put("usingsid",spot_pk_);

    std::string heads = json::encode(head);
    std::string bodys = json::encode(body);
    decltype(read_size_) u;
    u.h[0] = htons(static_cast<short>(heads.size()));
    u.h[1] = htons(static_cast<short>(bodys.size()));

    this->write(std::string(&u.s[0],sizeof(u)) + heads + bodys);
    BOOST_LOG_TRIVIAL(trace) << "im hello " << time(0);
  }

  void write(const std::string& msg)
  {
    io_service_.post(
        [this, msg]() {
          bool write_in_progress = !write_msgs_.empty();
          write_msgs_.push_back(msg);
          if (!write_in_progress)
            do_write();
        });
  }

  void do_write()
  {
    // std::cout << "do write " << write_msgs_.front() << "\n";
    asio::async_write(socket_,
        asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
        [this](boost::system::error_code ec, std::size_t /*length*/) {
          stage_tp_ = time(0) + 55;
          if (!ec) {
            // std::cout << "<<< "<< write_msgs_.front() <<" "<< ctime(&t) << std::endl;
            write_msgs_.pop_front();
            if (!write_msgs_.empty())
              do_write();
          } else {
            // close(ec); // socket_.close();
          }
        });
  }

private:
  asio::io_service& io_service_;
  tcp::socket socket_;
  std::string token_;
  asio::deadline_timer deadline_;

  std::pair<std::string,std::string> user_;
  std::shared_ptr<http_t> http_;

  union { uint16_t h[2]; char s[4]; uint32_t len; } read_size_;
  std::string read_msg_;

  message_queue write_msgs_;
};

#include <boost/program_options.hpp>
namespace po = boost::program_options;

std::string args(int ac, char * const av[])
{
  std::string fn; // std::pair<std::string,std::string> user;

  po::options_description opt_desc("Options");
  opt_desc.add_options()
    ("help", "display this help and exit")
    ("host", po::value<std::string>(&host_)->default_value(host_), "host to connect")
    ("port", po::value<std::string>(&port_)->default_value(port_), "port to connect")
    // ("mac", po::value<std::string>(&mac_)->default_value(mac_), "the macaddress")
    ("spot", po::value<std::string>(&spot_pk_)->default_value(spot_pk_), "spot group-id")
    ("file,f", po::value<std::string>(&fn)->required(), "login user list in file")
    ;
  po::positional_options_description pos_desc;
  pos_desc.add("file", 1);
    BOOST_LOG_TRIVIAL(trace) << __LINE__;

  po::variables_map vm;
  po::store(po::command_line_parser(ac, av)
      .options(opt_desc)
      .positional(pos_desc)
      .run(), vm);
    BOOST_LOG_TRIVIAL(trace) << __LINE__;
  if (vm.count("help"))
  {
    std::cout << "Usage: a.out [options] <token>\n"
      << opt_desc;
    exit(0);
  }

  po::notify(vm);

  return fn;
}

std::vector<std::shared_ptr<chat_client>> add_clients(std::string const & fn
        , asio::io_service & io_service)
{
    std::vector<std::shared_ptr<chat_client>> clients; // emplace_back

    tcp::resolver resolver(io_service);
    tcp::resolver::iterator iter = resolver.resolve({ host_, port_ });
    BOOST_LOG_TRIVIAL(trace) << host_;

    std::ifstream fin(fn.c_str());
    std::string line;
    while(std::getline(fin, line))
    {
        boost::regex e("^(\\S+)\\s+(\\S+)$");
        boost::smatch what;
        if (boost::regex_match(line, what, e))
        {
            clients.push_back(std::make_shared<chat_client>(io_service, iter, std::make_pair(what[1], what[2])));
        }
    }

    BOOST_LOG_TRIVIAL(trace) << clients.size();
    return clients;
}

int main(int ac, char* const av[])
{
  try
  {
    asio::io_service io_service;

    std::vector<std::shared_ptr<chat_client>> clients = add_clients(args(ac, av), io_service); // emplace_back

    // c.hello();

    // std::thread t([&io_service](){ io_service.run(); });
    // while (c.is_open())
    //{
        // c.hello();
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

