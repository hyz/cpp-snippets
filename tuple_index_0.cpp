#include<tuple>
#include<type_traits>
#include<string>
#include<iostream>
#include<boost/static_assert.hpp>

namespace detail {
    template<class Search, class Tp, int X>
    struct Pred : std::is_same<Search,typename std::tuple_element<0,typename std::tuple_element<X,Tp>::type>::type>
    {};
    //, typename std::conditional<std::is_same<Search,typename std::tuple_element<0,First>::type>::value,First,void>::type
    template<int X, class Search, class Tp, int b>
    struct index_0 : index_0<X-1, Search, Tp, Pred<Search,Tp,X-1>::value>
    {};
    template<int X, class Search, class Tp>
    struct index_0<X, Search, Tp, 1>
    {
        BOOST_STATIC_ASSERT(X>=0);
        static constexpr int index = X;
    };
} // namespace detail

template<typename Search, typename Tp>
struct index_first
{
    static constexpr int value = detail::index_0<std::tuple_size<Tp>::value, Search, Tp, 0>::index;
};

int main()
{
    using namespace std;

    typedef tuple<tuple<int, double>, tuple<string>, tuple<double, long>> Tuple;

    std::cout << index_first<int,Tuple>::value
        << index_first<string,Tuple>::value
        << index_first<double,Tuple>::value
        //<< index_first<tuple<int>,Tuple>::value
        << "\n";
}

