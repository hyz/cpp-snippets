// http://stackoverflow.com/questions/4819936/why-no-default-move-assignment-move-constructor
//
#include <vector>
#include <iostream>

struct C1
{
    std::vector<int> v;
    C1(std::initializer_list<int> l) : v(l) {}

    friend std::ostream & operator<<(std::ostream &os, C1 const& c) {
        for (auto& x: c.v)
            os << x;
        return os;
    }
};

struct C2
    : std::vector<int>
{
    using std::vector<int>::vector;

    friend std::ostream & operator<<(std::ostream &os, C2 const& c) {
        for (auto& x: c)
            os << x;
        return os;
    }
};

template <typename T>
void foo(T a)
{
    T b(std::move(a));
    T c{1,1,1};
    c = (std::move(b));

    std::cout
        << a <<"\n"
        << b <<"\n"
        << c <<"\n"
        ;
}

int main()
{
    C1 a{1,2,3,4,5};//{11};
    foo(a);
    C2 b{1,2,3,4,5};//{11};
    foo(b);
}

