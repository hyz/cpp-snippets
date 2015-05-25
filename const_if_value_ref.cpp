#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <boost/variant.hpp>

template <bool,typename T> struct const_if { typedef typename std::decay<T>::type type; };
template <typename T> struct const_if<true,T> { typedef const typename std::decay<T>::type type; };
template <typename T> struct is_pair : std::false_type {};
template <typename K, typename V> struct is_pair<std::pair<K, V>> : std::true_type {};

template <typename Iter> auto value_ref(Iter it)
    -> typename const_if<std::is_const<typename std::remove_reference<typename std::iterator_traits<Iter>::reference>::type>::value
            , typename std::iterator_traits<Iter>::value_type::second_type>::type&
{
    return it->second;
}
template <typename Iter> auto value_ref(Iter it)
    -> typename std::enable_if<!is_pair<typename std::iterator_traits<Iter>::value_type>::value
        , typename std::iterator_traits<Iter>::reference>::type
{
    return *it;
}

typedef boost::variant<int,float,std::string> variant;

template <typename T, typename O>
auto ref(O& o, variant const* = static_cast<O*>(0))
            -> typename const_if<std::is_const<O>::value,T>::type&
{
    auto* p = boost::get<T>(&o);
    if (!p) ;//BOOST_THROW_EXCEPTION( json::error_value() );
    return *p; 
}

void test_is_const()
{
    variant vs = "string";
    const variant vi = int(123);
    variant vf = float(1.23);

    std::cout
        << ref<std::string>(vs)
            <<" "<< std::is_const<typename std::remove_reference<decltype(ref<std::string>(vs))>::type>::value
        <<"\n"<< ref<int>(vi)
            <<" "<< std::is_const<typename std::remove_reference<decltype(ref<int>(vi))>::type>::value
        <<"\n"<< ref<float>(vf)
            <<" "<< std::is_const<typename std::remove_reference<decltype(ref<float>(vf))>::type>::value
        <<"\n";
}

void test_value_ref()
{
    {
        std::map<int,std::string> C{{1,"001"},{2,"002"}};
        //std::cout << is_pair<typename decltype(C.begin())::value_type>::value <<"\n";
        {
            typedef typename std::remove_reference<decltype(value_ref( C.begin()))>::type type;
            std::cout << value_ref(C.begin())
                <<" "<< std::is_const<type>::value <<"\n";

        } {
    std::map<int,std::string> const& Cc = C;
            typedef typename std::remove_reference<decltype(value_ref(Cc.begin()))>::type type;
            std::cout << value_ref(Cc.begin())
                <<" "<< std::is_const<type>::value <<"\n";
        }
    }{
        std::vector<std::string> C{"001","002"};
        {
            typedef typename std::remove_reference<decltype(value_ref( C.begin()))>::type type;
            std::cout << value_ref(C.begin())
                <<" "<< std::is_const<type>::value <<"\n";

        } {
    std::vector<std::string> const& Cc = C;
            typedef typename std::remove_reference<decltype(value_ref(Cc.begin()))>::type type;
            std::cout << value_ref(Cc.begin())
                <<" "<< std::is_const<type>::value <<"\n";
        }
    }
}

int main()
{
    test_value_ref();
}

