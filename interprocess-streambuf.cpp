#include <vector>
#include <boost/interprocess/streams/vectorstream.hpp>
#include <boost/interprocess/streams/bufferstream.hpp>

#include <iostream>

int __main()
{
    typedef boost::interprocess::basic_vectorbuf<std::vector<char> >        vectorbuf;
    vectorbuf buf;
    vectorbuf buf2;
    {
        std::ostream os(&buf);
        os << "hello world";
    }
    {
        std::vector<char> b;
        buf.swap_vector(b);

        {
            std::ostream os(&buf);
            os << "hello world";
        }

        std::cout << (b == buf.vector()) <<"\n";
    }
    ;
}

int main()
{
    typedef boost::interprocess::basic_bufferbuf<char>        bufferbuf;
    std::string str = "hello world";

    {
        bufferbuf buf(&str[0], str.size(), std::ios_base::in);

        std::string hello , world;
        std::iostream is(&buf);
        is >> hello >> world;
        std::cout << hello <<"_"<< world <<"\n";
    } {
        bufferbuf buf(&str[0], str.size(), std::ios_base::in);

        std::string hello , world;
        std::iostream is(&buf);
        is >> hello >> world;
        std::cout << hello <<"_"<< world <<"\n";
    }
}

