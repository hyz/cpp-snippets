#include <list>
#include <string>
#include <iostream>
#include <boost/format.hpp>

using namespace std;
using namespace boost;

int main(int ac, char *const av[])
{
    format fmt("%2% %1%");
    fmt % av[1];
    fmt % av[2];
    cout << fmt << endl;
    return 0;
}

