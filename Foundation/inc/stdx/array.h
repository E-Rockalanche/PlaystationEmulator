#pragma once

#include <array>
#include <type_traits>

namespace stdx
{

namespace details
{
    template <typename D, typename... Types>
    struct make_array_element
    {
        using type = D;
    };

    template <typename... Types>
    struct make_array_element<void, Types...> : std::common_type_t<Types...> {};

    template <typename D, typename... Types>
    using make_array_return_type = std::array<typename make_array_element<D, Types...>::type, sizeof...( Types )>;
}

template <typename D = void, typename... Types>
constexpr details::make_array_return_type<D, Types...> make_array( Types&&... t )
{
    return { std::forward<Types>( t )... };
}

}