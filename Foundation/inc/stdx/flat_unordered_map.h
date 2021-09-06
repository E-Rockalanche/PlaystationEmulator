#pragma once

#include <algorithm>
#include <utility>
#include <vector>

namespace stdx
{

template <typename Key, typename T>
class flat_unordered_map
{
	using storage_type = std::vector<std::pair<Key, T>>;

public:
	using key_type = Key;
	using mapped_type = T;
	using value_type = std::pair<const Key, T>;

	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

	using reference = value_type&;
	using const_reference = const value_type&;

	using iterator = typename storage_type::iterator;
	using const_iterator = typename storage_type::const_iterator;
	using reverse_iterator = typename storage_type::reverse_iterator;
	using const_reverse_iterator = typename storage_type::const_reverse_iterator;

	// construction/assignment

	flat_unordered_map() = default;

	template <typename InputIt>
	flat_unordered_map( InputIt first, InputIt last )
	{
		insert( first, last );
	}

	flat_unordered_map( const flat_unordered_map& ) = default;

	flat_unordered_map( flat_unordered_map&& ) = default;

	flat_unordered_map( std::initializer_list<value_type> init )
	{
		insert( init.begin(), init.end() );
	}

	flat_unordered_map& operator=( const flat_unordered_map& ) = default;

	flat_unordered_map& operator=( flat_unordered_map&& ) = default;

	flat_unordered_map& operator=( std::initializer_list<value_type> init )
	{
		return *this = flat_unordered_map{ init };
	}

	// element access

	T& at( const Key& key )
	{
		auto it = find( key );
		if ( it == end() )
			throw std::out_of_range( "Key out of range in flat_unordered_map" );

		return it->second;
	}

	const T& at( const Key& key ) const
	{
		auto it = find( key );
		if ( it == end() )
			throw std::out_of_range( "Key out of range in flat_unordered_map" );

		return it->second;
	}

	T& operator[]( const Key& key )
	{
		auto it = find( key );
		if ( it == end() )
			it = insert( { key, T() } );

		return it->second;
	}

	T& operator[]( Key&& key )
	{
		auto it = find( key );
		if ( it == end() )
			it = insert( { std::move( key ), T() } );

		return it->second;
	}

	// iterators

	iterator begin() noexcept { return m_pairs.begin(); }
	iterator end() noexcept { return m_pairs.end(); }

	const_iterator begin() const noexcept { return m_pairs.begin(); }
	const_iterator end() const noexcept { return m_pairs.end(); }

	const_iterator cbegin() const noexcept { return m_pairs.cbegin(); }
	const_iterator cend() const noexcept { return m_pairs.cend(); }

	reverse_iterator rbegin() noexcept { return m_pairs.rbegin(); }
	reverse_iterator rend() noexcept { return m_pairs.rend(); }

	const_reverse_iterator rbegin() const noexcept { return m_pairs.rbegin(); }
	const_reverse_iterator rend() const noexcept { return m_pairs.rend(); }

	const_reverse_iterator crbegin() const noexcept { return m_pairs.crbegin(); }
	const_reverse_iterator crend() const noexcept { return m_pairs.crend(); }

	bool empty() const noexcept { return m_pairs.empty(); }
	size_type size() const noexcept { return m_pairs.size(); }
	size_type max_size() const noexcept { return m_pairs.max_size(); }

	// modifiers

	void clear() { m_pairs.clear(); }

	template <typename P>
	std::pair<iterator, bool> insert( P&& value )
	{
		auto it = find( value.first );
		if ( it == end() )
		{
			m_pairs.push_back( std::forward<P>( value ) );
			return { end() - 1, true };
		}
		else
		{
			return { it, false };
		}
	}

	template <typename InputIt>
	void insert( InputIt first, InputIt last )
	{
		m_pairs.reserve( m_pairs.size() + std::distance( first, last ) );
		for ( ; first != last; ++first )
			insert( *first );
	}

	void insert( std::initializer_list<value_type> init )
	{
		insert( init.begin(), init.end() );
	}

	template <typename K, typename M>
	std::pair<iterator, bool> insert_or_assign( K&& k, M&& obj )
	{
		auto it = find( k );
		if ( it == end() )
		{
			m_pairs.push_back( { std::forward<K>( k ), std::forward<M>( obj ) } );
			return { end() - 1, true };
		}
		else
		{
			it->second = std::forward<M>( obj );
			return { it, false };
		}
	}

	template <typename... Args>
	std::pair<iterator, bool> emplace( Args&&... args )
	{
		return insert( std::pair<Key, T>( std::forward<Args>( args )... ) );
	}

	iterator erase( iterator pos )
	{
		return m_pairs.erase( pos );
	}

	iterator erase( iterator first, iterator last )
	{
		return m_pairs.erase( first, last );
	}

	size_type erase( const Key& key )
	{
		auto it = find( key );
		if ( it != end() )
		{
			m_pairs.erase( it );
			return 1;
		}
		else
		{
			return 0;
		}
	}

	void swap( flat_unordered_map& other )
	{
		m_pairs.swap( other.m_pairs );
	}

	// lookup

	size_type count( const Key& key ) const
	{
		return contains( key ) ? 1 : 0;
	}

	template <typename K>
	size_type count( const K& x ) const
	{
		return static_cast<size_type>( std::count_if( begin(), end(), []( auto& value ) { return value.first == x; } ) );
	}

	iterator find( const Key& key )
	{
		return std::find_if( begin(), end(), [&key]( auto& value ) { return value.first == key; } );
	}

	const_iterator find( const Key& key ) const
	{
		return std::find_if( begin(), end(), [&key]( auto& value ) { return value.first == key; } );
	}

	template <typename K>
	iterator find( const K& x )
	{
		return std::find_if( begin(), end(), [&x]( auto& value ) { return value.first == x; } );
	}

	template <typename K>
	const_iterator find( const K& x ) const
	{
		return std::find_if( begin(), end(), [&x]( auto& value ) { return value.first == x; } );
	}

	bool contains( const Key& key ) const
	{
		return std::any_of( begin(), end(), [&key]( auto& value ) { return value.first == key; } );
	}

	template <typename K>
	bool contains( const K& x ) const
	{
		return std::any_of( begin(), end(), [&x]( auto& value ) { return value.first == x; } );
	}

	friend bool operator==( const flat_unordered_map& lhs, const flat_unordered_map& rhs )
	{
		if ( lhs.size() != rhs.size() )
			return false;

		return std::all_of( lhs.begin(), lhs.end(), [&rhs]( auto& value )
			{
				auto it = rhs.find( value.first );
				return ( it != rhs.end() ) && ( it->second == value.second );
			} );
	}

	friend bool operator!=( const flat_unordered_map& lhs, const flat_unordered_map& rhs )
	{
		return !( lhs == rhs );
	}

private:
	storage_type m_pairs;
};

}