#include <tuple>

namespace detail
{
    template<typename T, typename IE, int I>
    struct same {
        template<typename... A> static T* test(std::tuple<A...>& t) {
            typedef typename std::tuple_element<I-1, std::tuple<A...> >::type X;
            return same<T,X,I-1>::test(t);
        }
    };
    template<typename T, int I>
    struct same<T,T,I> {
        template<typename... A> static T* test(std::tuple<A...>& t) { return &std::get<I>(t); }
    };
    template<typename T, typename IE>
    struct same<T,IE,0> {
        template<typename... A> static T* test(std::tuple<A...>& t) { return (T*)(0); }
    };
    template<typename T>
    struct same<T,T,0> {
        template<typename... A> static T* test(std::tuple<A...>& t) { return &std::get<0>(t); }
    };
}

template<typename T, typename... Args>
T* object(std::tuple<Args...>& t)
{
    return detail::same<T, void, sizeof...(Args)>::test(t);
}

#include <iostream>
int main()
{
    std::tuple<bool, char, int, float> a(0, 1,2,3);
    std::cout << *object<float>(a) << std::endl; // Prints 2
    std::cout << *object<int>(a) << std::endl; // Prints 0
    std::cout << (int)*object<char>(a) << std::endl; // Prints -1 (not found)
    std::cout << (int)*object<bool>(a) << std::endl; // Prints -1 (not found)
}

