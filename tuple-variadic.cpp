#include <vector>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

// static constexpr

// template<size_t N>
// struct applyF_ {
//     template<typename F, typename T, typename... A>
//     static inline auto Fw(F && f, T && t, A &&... a) -> decltype(
//         applyF_<N-1>::Fw(std::forward<F>(f), std::forward<T>(t), std::get<N-1>(std::forward<T>(t)), std::forward<A>(a)...))
//     {
// return  applyF_<N-1>::Fw(std::forward<F>(f), std::forward<T>(t), std::get<N-1>(std::forward<T>(t)), std::forward<A>(a)...);
//     }
// };
// 
// template<>
// struct applyF_<0> {
//     template<typename F, typename T, typename... A>
//     static inline auto Fw(F && f, T &&, A &&... a) -> decltype(
//                 std::forward<F>(f)(std::forward<A>(a)...))
//     {
//         //return  std::forward<F>(f)(std::forward<A>(a)...);
//         return  (f)(std::forward<A>(a)...);
//     }
// };
// 
// template<typename F, typename T>
// inline auto apply(F && f, T && t) -> decltype(
//         applyF_<std::tuple_size<typename std::decay<T>::type>::value>::Fw(std::forward<F>(f), std::forward<T>(t)))
// {
// return  applyF_<std::tuple_size<typename std::decay<T>::type>::value>::Fw(std::forward<F>(f), std::forward<T>(t));
// }

//////////////////
// http://stackoverflow.com/questions/7858817/unpacking-a-tuple-to-call-a-matching-function-pointer?lq=1
//

namespace detail {
    template<int...S> struct seq {
        template <typename F, typename T> static inline void invoke(F&& f, T&& t) { f(std::get<S>(t)...); }
    };
    template<int N, int ...S> struct gens : gens<N-1, N-1, S...> {};
    template<int ...S> struct gens<0, S...> { typedef seq<S...> type; };
} // namespace detail {}

template <typename F, typename T>
void apply(F&& f, T&& t) {
    typedef typename detail::gens<std::tuple_size<typename std::remove_reference<T>::type>::value>::type seq_;
    seq_::invoke(std::forward<F>(f), std::forward<T>(t));
}

#include <iostream>

struct Foo
{
    void operator()(int x, const char* y) //const
    {
        std::cout << x <<" "<< y;
    }
    void operator()(int x, std::tuple<int,char const*> & t) //const
    {
        std::cout << x <<" ";
        apply(*this, t);
    }
};

int _1_main()
{
    auto t2 = std::make_tuple(11, "foo");
    auto t22 = std::make_tuple(11, std::make_tuple(22, "bar"));
    Foo foo;
    apply(foo, t22); puts("");
    apply(Foo(), t22); puts("");
    //apply(Foo(), t2); puts("");
    // apply(Foo(), t22); puts("");
    return 0;
}

#include <boost/variant/variant.hpp>

namespace detail {
    template <typename> struct variant;
    template <template<typename...> class Tuple, typename... T>
    struct variant<Tuple<T...>> {
        typedef boost::variant<T...> type;
    };
    template <typename> struct tuple;
    template <template<typename...> class Variant, typename... T>
    struct tuple<Variant<T...>> {
        typedef std::tuple<T...> type;
    };
} // namespace type

int main()
{
    typedef std::tuple<int,std::string,std::vector<int>> Tuple0;
    typedef detail::variant<Tuple0>::type Variant;
    typedef detail::tuple<Variant>::type Tuple;

    std::cout << "sizeof"
        <<"\n tuple " << sizeof(Tuple0) <<" "<< typeid(Tuple0).name()
        <<"\n variant " << sizeof(Variant) // <<" "<< typeid(Tuple0).name()
        <<"\n tuple " << sizeof(Tuple) <<" "<< typeid(Tuple).name()
        <<"\n";

    return 0;
}

