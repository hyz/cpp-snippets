#include <iostream>
#include <boost/asio.hpp>

int main()
{
    namespace ip = boost::asio::ip;

    try {
        boost::asio::io_service io_s;
        ip::udp::resolver   resolver(io_s);
        ip::udp::resolver::query query(ip::udp::v4(), "8.8.8.8", "");
        ip::udp::resolver::iterator iter = resolver.resolve(query);
        ip::udp::endpoint ep = *iter;
        ip::udp::socket socket(io_s);
        socket.connect(ep);
        ip::address addr = socket.local_endpoint().address();
        std::cout << "My IP according to 8.8.8.8 is: " << addr.to_string() << std::endl;
    } catch (std::exception& e){
        std::cerr << "Could not deal with socket. Exception: " << e.what() << std::endl;
    }
}

int use_tcp()
{
    namespace ip = boost::asio::ip;

    try {
        boost::asio::io_service io_s;
        ip::tcp::resolver resolver(io_s);
        ip::tcp::resolver::query query(ip::tcp::v4(), "baidu.com", "80");
        ip::tcp::resolver::iterator iter = resolver.resolve(query);
        ip::tcp::endpoint ep = *iter;
        ip::tcp::socket socket(io_s);
        socket.connect(ep);
        ip::address addr = socket.local_endpoint().address();
        std::cout << "My IP according to baidu is: " << addr.to_string() << std::endl;
    } catch (std::exception& e){
        std::cerr << "Could not deal with socket. Exception: " << e.what() << std::endl;
    }
    return 0;
}

