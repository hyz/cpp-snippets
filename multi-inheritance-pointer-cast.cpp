
#include <string>
#include <iostream>

struct B1 { int x=0; };
struct B2 { int x=0; };
struct D : B1, B2 { int x=0; };

template <typename X, typename Y>
void print_1(X* x, Y* y)
{
    std::cout << std::hex
        <<"\t X * "<< x
        <<"\t Y * "<< static_cast<Y*>( static_cast<D*>(x) )
        <<"\t D * "<< static_cast<D*>(x)
        <<"\n";
}

void print(D* pd) {
    B1* p1 = pd;
    B2* p2 = pd;

    std::cout << std::hex
        <<"\tB1 * "<< p1
        <<"\tB2 * "<< p2
        <<"\tD  * "<< pd
        <<"\n";

    print_1(p1, p2);
    print_1(p2, p1);
}

int main()
{
    print(0);

    { D obj; print(&obj); }
    { D* pd=static_cast<D*>((D*)100); print(pd); }
}

