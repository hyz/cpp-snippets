#include <vector>
#include <algorithm>
#include <iostream>

using namespace std;

struct T
{
    int get() const { return 1; }
    T() {}
};

int main()
{
    []{}();
    [](){}();

    vector<T> v {T(), T(), T()};

    auto it = find_if( v.begin(), v.end(), [&](T& t){return t.get()==1;} );

    cout << distance(it,v.end());

    return 0;
}

