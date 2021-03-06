#include "MemoryControl.h"

#include "SaveState.h"

namespace PSX
{

void MemoryControl::Reset()
{
	m_expension1BaseAddress = 0;
	m_expension2BaseAddress = 0;
	m_comDelay.value = 0;
	m_ramSize.value = 0;
	m_cacheControl = 0;

	for ( auto& delaySize : m_delaySizes )
	{
		delaySize = {};
		CalculateAccessTime( delaySize );
	}
}

uint32_t MemoryControl::Read( uint32_t index ) const noexcept
{
	dbExpects( index < 9 );
	switch ( index )
	{
		case 0:
			return m_expension1BaseAddress;

		case 1:
			return m_expension2BaseAddress;

		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			return m_delaySizes[ index - 2 ].reg.value;

		case 8:
			return m_comDelay.value;

		default:
			dbBreak();
			return 0xffffffff;
	}
}

void MemoryControl::Write( uint32_t index, uint32_t value ) noexcept
{
	switch ( index )
	{
		case 0:
			m_expension1BaseAddress = 0x1f000000 | ( value & 0x00ffffff );
			break;

		case 1:
			m_expension2BaseAddress = 0x1f000000 | ( value & 0x00ffffff );
			break;

		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		{
			const uint32_t newValue = value & DelaySizeRegister::WriteMask;
			auto& delaySize = m_delaySizes[ index - 2 ];
			if ( newValue != delaySize.reg.value )
			{
				delaySize.reg.value = newValue;
				CalculateAccessTime( delaySize );
			}
			break;
		}

		case 8:
		{
			const uint32_t newValue = value & ComDelay::WriteMask;
			if ( newValue != m_comDelay.value )
			{
				m_comDelay.value = newValue;
				for ( auto& delaySize : m_delaySizes )
					CalculateAccessTime( delaySize );
			}
			break;
		}

		default:
			dbBreak();
			break;
	}
}

void MemoryControl::CalculateAccessTime( DelaySize& delaySize ) noexcept
{
	int32_t first = 0;
	int32_t seq = 0;

	auto& reg = delaySize.reg;

	if ( reg.useCom0Time )
	{
		first += m_comDelay.com0 - 1;
		seq += m_comDelay.com0 - 1;
	}

	if ( reg.useCom2Time )
	{
		first += m_comDelay.com2;
		seq += m_comDelay.com2;
	}

	if ( first < 6 )
		first += 1; // somewhat like so

	first += reg.accessTime + 2;
	seq += reg.accessTime + 2;

	const int32_t min = reg.useCom3Time ? m_comDelay.com3 : 0;

	first = std::max( first, min + 6 );
	seq = std::max( seq, min + 2 );

	const cycles_t accessTime8bit = first;
	const cycles_t accessTime16bit = reg.dataBusWidth ? first : ( first + seq );
	const cycles_t accessTime32bit = reg.dataBusWidth ? ( first + seq ) : ( first + seq * 3 );

	delaySize.accessTimes[ 0 ] = std::max( accessTime8bit - 1, 0 );
	delaySize.accessTimes[ 1 ] = std::max( accessTime16bit - 1, 0 );
	delaySize.accessTimes[ 2 ] = std::max( accessTime32bit - 1, 0 );
}

void MemoryControl::Serialize( SaveStateSerializer& serializer )
{
	if ( !serializer.Header( "MemoryControl", 2 ) )
		return;

	serializer( m_expension1BaseAddress );
	serializer( m_expension2BaseAddress );

	for ( auto& delay : m_delaySizes )
	{
		serializer( delay.reg.value );
		serializer( delay.accessTimes );
	}

	serializer( m_comDelay.value );

	serializer( m_ramSize.value );

	serializer( m_cacheControl );
}

}