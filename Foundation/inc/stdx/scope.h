#pragma once

namespace stdx
{

template <typename ExitFunction>
class scope_exit
{
public:
	explicit scope_exit( const ExitFunction& fn ) : m_onExit( fn ) {}

	explicit scope_exit( ExitFunction&& fn ) : m_onExit( std::move( fn ) ) {}

	scope_exit( scope_exit&& other )  noexcept : m_onExit( std::move( other.m_onExit ) ), m_active( std::exchange( other.m_active, false ) ) {}

	scope_exit( const scope_exit& ) = delete;

	~scope_exit()
	{
		if ( m_active )
			m_onExit();
	}

	scope_exit& operator=( scope_exit&& ) = delete;
	scope_exit& operator=( const scope_exit& ) = delete;

	void release()
	{
		m_active = false;
	}

private:
	ExitFunction m_onExit;
	bool m_active = true;
};

} // namespace stdx