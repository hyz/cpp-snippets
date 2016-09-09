#include <tuple>

namespace detail
{
    template<typename T, typename IE, int I>
    struct same {
        template<typename... Args>
        static T* test(std::tuple<Args...>& t) {
            typedef typename std::tuple_element<I-1, std::tuple<Args...> >::type X;
            return same<T,X,I-1>::test(t);
        }
    };
    template<typename T, typename IE>
    struct same<T,IE,0> {
        template<typename... Args>
        static T* test(std::tuple<Args...>& t) { return 0; }
    };
    template<typename T, int I>
    struct same<T,T,I> {
        template<typename... Args>
        static T* test(std::tuple<Args...>& t) { return &std::get<I>(t); }
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
    std::tuple<char, int, float> a(0,2,3);
    std::cout << *object<float>(a) << std::endl; // Prints 2
    std::cout << *object<int>(a) << std::endl; // Prints 0
    //std::cout << *object<char>(a) << std::endl; // Prints -1 (not found)
}

