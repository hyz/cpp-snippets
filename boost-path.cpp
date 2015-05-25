#include <stdint.h>
#include <iostream>
#include <istream>
#include <ostream>
#include <list>
#include <vector>
#include <map>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/regex.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/random.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>


#define RES_DIR "/tmp"

#define PHONE_SIZE 11
#define PASSWD_SIZE 25
#define NAME_SIZE   25
#define URL_PATH "/home/lindu/server/picture/"
#define WHISPER0  1
#define SEND      2
#define NOSEND    3

using namespace std;
using namespace boost;

int main(int ac, char *const av[])
{
    filesystem::path pa(av[1]);
    filesystem::path ext = pa.extension();
    cout << pa.is_absolute() << " " << ext << "\n";

    filesystem::path p1(RES_DIR);
    p1 /= pa;

    filesystem::path p2(RES_DIR "/");
    p2 /= pa;

    for (filesystem::path::iterator i = p2.begin();
            i != p2.end(); ++i)
        cout << *i << " ";
    cout << "\n";

    cout << p1 << "\t" << p2 << "\n";

    filesystem::ofstream out(p2);
    out << "hello";
}

