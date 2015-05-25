//#include <boost/asio.hpp>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/array.hpp>
#include <vector>

int main()
{
    // std::cout << sizeof(boost::asio::streambuf) << std::endl;
    std::cout << sizeof(std::streambuf) << std::endl;
    std::cout << sizeof(std::ostream) << std::endl;

    std::cout << sizeof(boost::asio::ip::tcp::socket) << std::endl;

    std::cout << sizeof(std::vector<int>) << std::endl;
    // std::cout << sizeof(boost::container::flat_map<std::string,std::string>) << std::endl;

    return 0;
}


