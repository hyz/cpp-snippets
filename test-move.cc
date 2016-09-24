#include <stdio.h>
#include <utility>

struct A {
    A() {
        printf("A\n");
    }
    A(A const& rhs) {
        printf("A const&\n");
    }
    A(A&& rhs) {
        printf("A&&\n");
    }
    A& operator=(A const& rhs) {
        printf("operator=(A const&)\n");
    }
    A& operator=(A&& rhs) {
        printf("operator=(A&&)\n");
    }
};

struct B : A {
    void foo() {}
};

int main() {
    B a;
    B b(a);
    B c(std::move(b));
    B d = B(); //! B d(B());
    B e = B(a); //! B d(B());
    e.foo();
}

