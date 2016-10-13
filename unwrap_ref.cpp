#include "boost/utility/result_of.hpp"
#include "boost/typeof/typeof.hpp"
#include "boost/assign.hpp"
#include "boost/ref.hpp"
#include "iostream"
using namespace std;

struct UserType {
    int x;
    UserType(int x_=0) : x(x_) { }
    friend std::ostream& operator<<(std::ostream& out, UserType const& c) {
        return out << (void*)&c <<' '<< c.x;
    }
};

template<typename T>
void Print(T t)
{
    boost::unwrap_reference<T>::type& a = boost::unwrap_ref(t);
    std::cout << a << std::endl;
    BOOST_AUTO(b, boost::unwrap_ref(a));
    std::cout << a << std::endl;
    BOOST_AUTO(c, boost::unwrap_ref(b));
    std::cout << a << std::endl;
}

int main(int argc, char*const argv[])
{
    UserType c1;
    Print(c1);

    BOOST_AUTO(rw, boost::ref(c1));
    Print(rw);

    Print(argc);

    return 0;
}
