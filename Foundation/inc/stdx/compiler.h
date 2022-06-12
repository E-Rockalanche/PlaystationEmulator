#pragma once

// c++20 concepts

#if ( __cplusplus <= 201703L )

namespace compiler_detail
{
// define our own enable_if so we don't need to include type_traits

template <bool Condition, typename T>
struct enable_if {};

template <typename T>
struct enable_if<true, T>
{
	using type = T;
};

template <bool Condition, typename T>
using enable_if_t = typename enable_if<Condition, T>::type;
}

#define STDX_requires( condition ) bool RequiresTag = true, ::compiler_detail::enable_if_t<( condition ), int> = 0>

#define STDX_concept inline constexpr bool

#define STDX_no_unique_address

#else

#define STDX_requires( condition ) bool RequiresTag = true> requires( condition )

#define STDX_concept concept

#define STDX_no_unique_address [[no_unique_address]]

#endif

// compiler intrinsics

#if defined _MSC_VER

#define STDX_assume( expression ) __assume( !!( expression ) )
#define STDX_likely( expression ) ( expression )
#define STDX_unlikely( expression ) ( expression )
#define STDX_unreachable() __assume( 0 )
#define STDX_forceinline __forceinline

#pragma warning( disable:4201 ) // nonstandard extension: nameless struct/union

#elif defined __GNUC__

#define STDX_assume( expression ) ( ( expression ) ? (void)0 : __builtin_unreachable() )
#define STDX_likely( expression ) __builtin_expect( !!( expression ), 1 )
#define STDX_unlikely( expression ) __builtin_expect( !!( expression ), 0 )
#define STDX_unreachable() __builtin_unreachable()
#define STDX_forceinline always_inline

#elif defined __clang__

#define STDX_assume( expression ) __builtin_assume( expression )
#define STDX_likely( expression ) __builtin_expect( !!( expression ), 1 )
#define STDX_unlikely( expression ) __builtin_expect( !!( expression ), 0 )
#define STDX_unreachable() __builtin_unreachable()
#define STDX_forceinline always_inline

#else

#define STDX_assume( expression ) EMPTY_BLOCK
#define STDX_likely( expression ) ( expression )
#define STDX_unlikely( expression ) ( expression )
#define STDX_unreachable() EMPTY_BLOCK
#define STDX_forceinline inline

#endif