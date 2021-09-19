#include "GeometryTransformationEngine.h"

#include <stdx/assert.h>
#include <stdx/bit.h>

#include <algorithm>

namespace PSX
{

namespace
{

template <typename T>
inline void ShiftBuffer( T& buffer )
{
	for ( size_t i = 1; i < buffer.size(); ++i )
		buffer[ i - 1 ] = buffer[ i ];
}

template <typename T>
constexpr uint32_t SignExtend16( T value ) noexcept
{
	static_assert( sizeof( T ) == sizeof( int16_t ) );
	return static_cast<uint32_t>( static_cast<int16_t>( value ) );
}

template <typename T, typename U>
constexpr int64_t DotProduct3( const T& lhs, const U& rhs )
{
	return int64_t( lhs[ 0 ] ) * int64_t( rhs[ 0 ] ) + int64_t( lhs[ 1 ] ) * int64_t( rhs[ 1 ] ) + int64_t( lhs[ 2 ] ) * int64_t( rhs[ 2 ] );
}

}

void GeometryTransformationEngine::Reset()
{
	m_vectors.fill( Math::Vector3<int16_t>{ 0 } );

	m_color = { 0 };
	m_code = 0;

	m_orderTableAvgZ = 0;

	m_ir0 = 0;
	m_ir123 = { 0 };

	m_screenXYFifo.fill( Math::Vector2<int16_t>{ 0 } );
	m_screenZFifo.fill( 0 );
	m_colorCodeFifo.fill( 0 );

	m_mac0 = 0;
	m_mac123 = { 0 };

	m_colorConversion = { 0 };

	m_leadingBitsSource = 0;
	m_leadingBitsResult = 0;

	m_rotation = Matrix( 0 );

	m_translation = { 0 };

	m_lightMatrix = Matrix( 0 );

	m_backgroundColor = { 0 };

	m_colorMatrix = Matrix( 0 );

	m_farColor = { 0 };

	m_screenOffset = { 0 };

	m_projectionPlaneDistance = 0;

	m_depthQueueParamA = 0;
	m_depthQueueParamB = 0;

	m_zScaleFactor3 = 0;
	m_zScaleFactor4 = 0;

	m_errorFlags = 0;
}

uint32_t GeometryTransformationEngine::Read( uint32_t index ) const noexcept
{
	dbExpects( index < 64 );

	// dbLog( "GeometryTransformationEngine::Read() -- [%u]", index );

	auto readVXYn = [this]( size_t n ) -> uint32_t
	{
		auto& v = m_vectors[ n ];
		return static_cast<uint16_t>( v.x ) | ( static_cast<uint16_t>( v.y ) << 16 );
	};

	auto readVZn = [this]( size_t n ) -> uint32_t
	{
		return static_cast<uint16_t>( m_vectors[ n ].z );
	};

	auto readMatrixPair = []( const Matrix& matrix, size_t elementOffset ) -> uint32_t
	{
		return static_cast<uint16_t>( matrix.elements[ elementOffset ] ) |
			( static_cast<uint16_t>( matrix.elements[ elementOffset + 1 ] ) << 16 );
	};

	auto readScreenXYn = [this]( size_t n ) -> uint32_t
	{
		auto& v = m_screenXYFifo[ n ];
		return static_cast<uint16_t>( v.x ) | ( static_cast<uint16_t>( v.y ) << 16 );
	};

	switch ( static_cast<Register>( index ) )
	{
		case Register::VXY0:	return readVXYn( 0 );
		case Register::VXY1:	return readVXYn( 1 );
		case Register::VXY2:	return readVXYn( 2 );

		case Register::VZ0:		return readVZn( 0 );
		case Register::VZ1:		return readVZn( 1 );
		case Register::VZ2:		return readVZn( 2 );

		case Register::ColorCode:	return Math::ToColorCode( m_color ) | ( m_code << 24 );

		case Register::OrderTableAvgZ:	return m_orderTableAvgZ;

		case Register::IR0:		return static_cast<uint32_t>( m_ir0 );
		case Register::IR1:		return static_cast<uint32_t>( m_ir123.x );
		case Register::IR2:		return static_cast<uint32_t>( m_ir123.y );
		case Register::IR3:		return static_cast<uint32_t>( m_ir123.z );

		case Register::SXY0:	return readScreenXYn( 0 );
		case Register::SXY1:	return readScreenXYn( 1 );
		case Register::SXY2:	return readScreenXYn( 2 );
		case Register::SXYP:	return readScreenXYn( 2 ); // mirror of SXY2

		case Register::SZ0:		return m_screenZFifo[ 0 ];
		case Register::SZ1:		return m_screenZFifo[ 1 ];
		case Register::SZ2:		return m_screenZFifo[ 2 ];
		case Register::SZ3:		return m_screenZFifo[ 3 ];

		case Register::RGB0:	return m_colorCodeFifo[ 0 ];
		case Register::RGB1:	return m_colorCodeFifo[ 1 ];
		case Register::RGB2:	return m_colorCodeFifo[ 2 ];

		case Register::Prohibited:
			return 0;

		case Register::MAC0:	return static_cast<uint32_t>( m_mac0 );
		case Register::MAC1:	return static_cast<uint32_t>( m_mac123.x );
		case Register::MAC2:	return static_cast<uint32_t>( m_mac123.y );
		case Register::MAC3:	return static_cast<uint32_t>( m_mac123.z );

		case Register::ColorConversionInput:
		case Register::ColorConversionOutput:
			return static_cast<uint32_t>( m_colorConversion.r | ( m_colorConversion.g << 5 ) | ( m_colorConversion.b << 10 ) );

		case Register::LeadingBitsSource:	return m_leadingBitsSource;

		case Register::LeadingBitsResult:	return m_leadingBitsResult;

		case Register::RT11RT12:	return readMatrixPair( m_rotation, 0 );
		case Register::RT13RT21:	return readMatrixPair( m_rotation, 2 );
		case Register::RT22RT23:	return readMatrixPair( m_rotation, 4 );
		case Register::RT31RT32:	return readMatrixPair( m_rotation, 6 );
		case Register::RT33:		return SignExtend16( m_rotation[ 2 ][ 2 ] );

		case Register::TranslationX:	return static_cast<uint32_t>( m_translation.x );
		case Register::TranslationY:	return static_cast<uint32_t>( m_translation.y );
		case Register::TranslationZ:	return static_cast<uint32_t>( m_translation.z );

		case Register::L11L12:	return readMatrixPair( m_lightMatrix, 0 );
		case Register::L13L21:	return readMatrixPair( m_lightMatrix, 2 );
		case Register::L22L23:	return readMatrixPair( m_lightMatrix, 4 );
		case Register::L31L32:	return readMatrixPair( m_lightMatrix, 6 );
		case Register::L33:		return SignExtend16( m_lightMatrix[ 2 ][ 2 ] );

		case Register::BackgroundRed:	return static_cast<uint32_t>( m_backgroundColor.x );
		case Register::BackgroundGreen:	return static_cast<uint32_t>( m_backgroundColor.y );
		case Register::BackgroundBlue:	return static_cast<uint32_t>( m_backgroundColor.z );

		case Register::LR1LR2:	return readMatrixPair( m_colorMatrix, 0 );
		case Register::LR3LG1:	return readMatrixPair( m_colorMatrix, 2 );
		case Register::LG2LG3:	return readMatrixPair( m_colorMatrix, 4 );
		case Register::LB1LB2:	return readMatrixPair( m_colorMatrix, 6 );
		case Register::LB3:		return SignExtend16( m_colorMatrix[ 2 ][ 2 ] );

		case Register::FarColorRed:		return static_cast<uint32_t>( m_farColor.x );
		case Register::FarColorGreen:	return static_cast<uint32_t>( m_farColor.y );
		case Register::FarColorBlue:	return static_cast<uint32_t>( m_farColor.z );

		case Register::ScreenOffsetX:	return static_cast<uint32_t>( m_screenOffset.x );
		case Register::ScreenOffsetY:	return static_cast<uint32_t>( m_screenOffset.y );

		case Register::ProjectionPlaneDistance:		return SignExtend16( m_projectionPlaneDistance );

		case Register::DepthQueueA:		return static_cast<uint16_t>( m_depthQueueParamA );
		case Register::DepthQueueB:		return static_cast<uint32_t>( m_depthQueueParamB );

		case Register::ZScaleFactor3:	return static_cast<uint16_t>( m_zScaleFactor3 );
		case Register::ZScaleFactor4:	return static_cast<uint16_t>( m_zScaleFactor4 );

		case Register::ErrorFlags:	return m_errorFlags;

		default:
			dbBreak();
			return 0;
	}
}

void GeometryTransformationEngine::Write( uint32_t index, uint32_t value ) noexcept
{
	dbExpects( index < 64 );

	// dbLog( "GeometryTransformationEngine::Write() -- [%u <- %X]", index, value );

	auto assignVXYn = [this]( size_t index, uint32_t value )
	{
		auto& v = m_vectors[ index ];
		v.x = static_cast<int16_t>( value );
		v.y = static_cast<int16_t>( value >> 16 );
	};

	auto assignVZn = [this]( size_t index, uint32_t value )
	{
		m_vectors[ index ].z = static_cast<int16_t>( value );
	};

	auto assignIRn = [this]( size_t index, uint32_t value )
	{
		m_ir123[ index ] = static_cast<int16_t>( value );
		m_colorConversion[ index ] = static_cast<uint8_t>( std::clamp<int16_t>( static_cast<int16_t>( value ) / 0x80, 0x00, 0x1f ) );
	};

	auto assignMatrixPair = [this]( Matrix& matrix, size_t elementOffset, uint32_t value )
	{
		matrix.elements[ elementOffset ] = static_cast<int16_t>( value );
		matrix.elements[ elementOffset + 1 ] = static_cast<int16_t>( value >> 16 );
	};

	auto assignScreenXY = [this]( size_t index, uint32_t value )
	{
		auto& v = m_screenXYFifo[ index ];
		v.x = static_cast<int16_t>( value );
		v.y = static_cast<int16_t>( value >> 16 );
	};

	switch ( static_cast<Register>( index ) )
	{
		case Register::VXY0:	assignVXYn( 0, value );	break;
		case Register::VXY1:	assignVXYn( 1, value );	break;
		case Register::VXY2:	assignVXYn( 2, value );	break;

		case Register::VZ0:		assignVZn( 0, value );	break;
		case Register::VZ1:		assignVZn( 1, value );	break;
		case Register::VZ2:		assignVZn( 2, value );	break;

		case Register::ColorCode:
			m_color = Math::FromColorCode<uint8_t>( value );
			m_code = value >> 24;
			break;

		case Register::OrderTableAvgZ:
			m_orderTableAvgZ = static_cast<uint16_t>( value );
			break;

		case Register::IR0:		m_ir0 = static_cast<int16_t>( value );
		case Register::IR1:		assignIRn( 0, value );	break;
		case Register::IR2:		assignIRn( 1, value );	break;
		case Register::IR3:		assignIRn( 2, value );	break;

		case Register::SXY0:	assignScreenXY( 0, value );		break;
		case Register::SXY1:	assignScreenXY( 1, value );		break;
		case Register::SXY2:	assignScreenXY( 2, value );		break;

		case Register::SXYP:
			ShiftBuffer( m_screenXYFifo );
			assignScreenXY( 2, value );
			break;

		case Register::SZ0:		m_screenZFifo[ 0 ] = static_cast<uint16_t>( value );	break;
		case Register::SZ1:		m_screenZFifo[ 1 ] = static_cast<uint16_t>( value );	break;
		case Register::SZ2:		m_screenZFifo[ 2 ] = static_cast<uint16_t>( value );	break;
		case Register::SZ3:		m_screenZFifo[ 3 ] = static_cast<uint16_t>( value );	break;

		case Register::RGB0:	m_colorCodeFifo[ 0 ] = value;	break;
		case Register::RGB1:	m_colorCodeFifo[ 1 ] = value;	break;
		case Register::RGB2:	m_colorCodeFifo[ 2 ] = value;	break;

		case Register::Prohibited:	break;

		case Register::MAC0:	m_mac0 = static_cast<int32_t>( value );			break;
		case Register::MAC1:	m_mac123.x = static_cast<int32_t>( value );		break;
		case Register::MAC2:	m_mac123.y = static_cast<int32_t>( value );		break;
		case Register::MAC3:	m_mac123.z = static_cast<int32_t>( value );		break;

		case Register::ColorConversionInput:
		{
			m_colorConversion.r = value & 0x1f;
			m_colorConversion.g = ( value >> 5 ) & 0x1f;
			m_colorConversion.b = ( value >> 10 ) & 0x1f;

			for ( size_t i = 0; i < 3; ++i )
				m_ir123[ i ] = static_cast<int16_t>( m_colorConversion[ i ] << 7 );

			break;
		}

		case Register::ColorConversionOutput:	break; // read only

		case Register::LeadingBitsSource:	m_leadingBitsSource = value;	break;

		case Register::LeadingBitsResult:	break; // read only

		case Register::RT11RT12:	assignMatrixPair( m_rotation, 0, value );	break;
		case Register::RT13RT21:	assignMatrixPair( m_rotation, 2, value );	break;
		case Register::RT22RT23:	assignMatrixPair( m_rotation, 4, value );	break;
		case Register::RT31RT32:	assignMatrixPair( m_rotation, 6, value );	break;
		case Register::RT33:		m_rotation[ 2 ][ 2 ] = static_cast<int16_t>( value );	break;

		case Register::TranslationX:	m_translation.x = static_cast<int32_t>( value );	break;
		case Register::TranslationY:	m_translation.y = static_cast<int32_t>( value );	break;
		case Register::TranslationZ:	m_translation.z = static_cast<int32_t>( value );	break;

		case Register::L11L12:	assignMatrixPair( m_lightMatrix, 0, value );	break;
		case Register::L13L21:	assignMatrixPair( m_lightMatrix, 2, value );	break;
		case Register::L22L23:	assignMatrixPair( m_lightMatrix, 4, value );	break;
		case Register::L31L32:	assignMatrixPair( m_lightMatrix, 6, value );	break;
		case Register::L33:		m_lightMatrix[ 2 ][ 2 ] = static_cast<int16_t>( value );	break;

		case Register::BackgroundRed:	m_backgroundColor.x = static_cast<int32_t>( value );	break;
		case Register::BackgroundGreen:	m_backgroundColor.y = static_cast<int32_t>( value );	break;
		case Register::BackgroundBlue:	m_backgroundColor.z = static_cast<int32_t>( value );	break;

		case Register::LR1LR2:	assignMatrixPair( m_colorMatrix, 0, value );		break;
		case Register::LR3LG1:	assignMatrixPair( m_colorMatrix, 2, value );		break;
		case Register::LG2LG3:	assignMatrixPair( m_colorMatrix, 4, value );		break;
		case Register::LB1LB2:	assignMatrixPair( m_colorMatrix, 6, value );		break;
		case Register::LB3:		m_colorMatrix[ 2 ][ 2 ] = static_cast<int16_t>( value );		break;

		case Register::FarColorRed:		m_farColor.x = static_cast<int32_t>( value );	break;
		case Register::FarColorGreen:	m_farColor.y = static_cast<int32_t>( value );	break;
		case Register::FarColorBlue:	m_farColor.z = static_cast<int32_t>( value );	break;

		case Register::ScreenOffsetX:	m_screenOffset.x = static_cast<int32_t>( value );	break;
		case Register::ScreenOffsetY:	m_screenOffset.y = static_cast<int32_t>( value );	break;

		case Register::ProjectionPlaneDistance:	m_projectionPlaneDistance = static_cast<uint16_t>( value );		break;

		case Register::DepthQueueA:		m_depthQueueParamA = static_cast<int16_t>( value );		break;
		case Register::DepthQueueB:		m_depthQueueParamB = static_cast<int32_t>( value );		break;

		case Register::ZScaleFactor3:	m_zScaleFactor3 = static_cast<int16_t>( value );	break;
		case Register::ZScaleFactor4:	m_zScaleFactor4 = static_cast<int16_t>( value );	break;

		case Register::ErrorFlags:
		{
			m_errorFlags = value & ErrorFlag::WriteMask;
			const bool hasError = m_errorFlags & ErrorFlag::ErrorMask;
			stdx::set_bits<uint32_t>( m_errorFlags, ErrorFlag::Error, hasError );
			break;
		}

		default:
			dbBreak(); // TODO
			break;
	}
}

template <size_t Index>
void GeometryTransformationEngine::SetMAC( int64_t value, int shiftAmount ) noexcept
{
	dbExpects( shiftAmount == 0 || Index != 0 );

	static constexpr int64_t Min = ( Index == 0 ) ? MAC0Min : MAC123Min;
	static constexpr int64_t Max = ( Index == 0 ) ? MAC0Max : MAC123Max;
	if ( value < Min )
	{
		if constexpr ( Index == 0 )
			m_errorFlags |= ErrorFlag::MAC0Underflow;
		if constexpr ( Index == 1 )
			m_errorFlags |= ErrorFlag::MAC1Underflow;
		if constexpr ( Index == 2 )
			m_errorFlags |= ErrorFlag::MAC2Underflow;
		if constexpr ( Index == 3 )
			m_errorFlags |= ErrorFlag::MAC3Underflow;
	}
	else if ( value > Max )
	{
		if constexpr ( Index == 0 )
			m_errorFlags |= ErrorFlag::MAC0Overflow;
		if constexpr ( Index == 1 )
			m_errorFlags |= ErrorFlag::MAC1Overflow;
		if constexpr ( Index == 2 )
			m_errorFlags |= ErrorFlag::MAC2Overflow;
		if constexpr ( Index == 3 )
			m_errorFlags |= ErrorFlag::MAC3Overflow;
	}

	value >>= shiftAmount;

	if constexpr ( Index == 0 )
		m_mac0 = static_cast<int32_t>( value );
	else
		m_mac123[ Index - 1 ] = static_cast<int32_t>( value );
}

template <size_t Index>
void GeometryTransformationEngine::SetIR( int32_t value, bool lm ) noexcept
{
	// duckstation lets ir0 be saturated

	static constexpr auto Min = ( Index == 0 ) ? IR0Min : IR123Min;
	static constexpr auto Max = ( Index == 0 ) ? IR0Max : IR123Max;

	const auto minValue = lm ? 0 : Min;
	if ( value < minValue || value > Max )
	{
		value = ( value < minValue ) ? minValue : Max;

		if constexpr ( Index == 0 )
			m_errorFlags |= ErrorFlag::IR0Saturated;
		if constexpr ( Index == 1 )
			m_errorFlags |= ErrorFlag::IR1Saturated;
		if constexpr ( Index == 2 )
			m_errorFlags |= ErrorFlag::IR2Saturated;
		if constexpr ( Index == 3 )
			m_errorFlags |= ErrorFlag::IR3Saturated;
	}

	if constexpr ( Index == 0 )
		m_ir0 = static_cast<int16_t>( value );
	else
		m_ir123[ Index - 1 ] = static_cast<int16_t>( value );
}

template <size_t Index>
void GeometryTransformationEngine::SetMACAndIR( int64_t value, int shiftAmount, bool lm ) noexcept
{
	SetMAC<Index>( value, shiftAmount );
	SetIR<Index>( static_cast<int32_t>( value >> shiftAmount ), lm );
}

template <size_t Component>
uint32_t GeometryTransformationEngine::TruncateRGB( int32_t value ) noexcept
{
	if ( value < ColorMin || value > ColorMax )
	{
		if constexpr ( Component == 0 )
			m_errorFlags |= ErrorFlag::ColorFifoRSaturated;
		if constexpr ( Component == 1 )
			m_errorFlags |= ErrorFlag::ColorFifoGSaturated;
		if constexpr ( Component == 2 )
			m_errorFlags |= ErrorFlag::ColorFifoBSaturated;

		return ( value < ColorMin ) ? ColorMin : ColorMax;
	}

	return static_cast<uint32_t>( value );
}

void GeometryTransformationEngine::PushScreenZ( int32_t value ) noexcept
{
	if ( value < ZMin )
	{
		value = ZMin;
		m_errorFlags |= ErrorFlag::SZ3OrOTZSaturated;
	}
	else if ( value > ZMax )
	{
		value = ZMax;
		m_errorFlags |= ErrorFlag::SZ3OrOTZSaturated;
	}

	ShiftBuffer( m_screenZFifo );
	m_screenZFifo.back() = static_cast<uint16_t>( value );
}

void GeometryTransformationEngine::PushScreenXY( int32_t x, int32_t y ) noexcept
{
	auto truncate = [this]( int32_t coord, uint32_t errorFlag ) -> int16_t
	{
		if ( coord < ScreenMin )
		{
			m_errorFlags |= errorFlag;
			return ScreenMin;
		}
		else if ( coord > ScreenMax )
		{
			m_errorFlags |= errorFlag;
			return ScreenMax;
		}
		return static_cast<int16_t>( coord );
	};

	ShiftBuffer( m_screenXYFifo );
	auto& pos = m_screenXYFifo.back();
	pos.x = truncate( x, ErrorFlag::SX2Saturated );
	pos.y = truncate( y, ErrorFlag::SY2Saturated );
}

void GeometryTransformationEngine::PushColor( int32_t r, int32_t g, int32_t b ) noexcept
{
	ShiftBuffer( m_colorCodeFifo );
	m_colorCodeFifo.back() = TruncateRGB<0>( r ) | ( TruncateRGB<1>( g ) << 8 ) | ( TruncateRGB<2>( b ) << 16 ) | ( m_code << 24 );
}

void GeometryTransformationEngine::CalculateAverageZ( size_t size, uint32_t zScaleFactor ) noexcept
{
	int64_t result = 0;
	for ( size_t i = 0; i < size; ++i )
		result += m_screenZFifo[ i ];

	result *= zScaleFactor;

	SetMAC<0>( result );

	result = m_mac0 / 0x1000;

	if ( result < ZMin )
	{
		result = ZMin;
		m_errorFlags |= ErrorFlag::SZ3OrOTZSaturated;
	}
	else if ( result > ZMax )
	{
		result = ZMax;
		m_errorFlags |= ErrorFlag::SZ3OrOTZSaturated;
	}

	m_orderTableAvgZ = static_cast<uint16_t>( result );
}

void GeometryTransformationEngine::Transform( const Matrix& matrix, const Vector16& vector, int shiftAmount, bool lm ) noexcept
{
#define MULT( N ) SetMACAndIR<(N)+1>( DotProduct3( matrix[ (N) ], vector ), shiftAmount, lm )
	MULT( 0 );
	MULT( 1 );
	MULT( 2 );
#undef MULT
}

void GeometryTransformationEngine::Transform( const Matrix& matrix, const Vector16& vector, const Vector32& translation, int shiftAmount, bool lm ) noexcept
{
#define MULT( N ) SetMACAndIR<(N)+1>( int64_t( translation[ (N) ] ) * int64_t( 0x1000 ) + DotProduct3( matrix[ (N) ], vector ), shiftAmount, lm )
	MULT( 0 );
	MULT( 1 );
	MULT( 2 );
#undef MULT
}

void GeometryTransformationEngine::ExecuteCommand( uint32_t commandValue ) noexcept
{
	// dbLog( "GeometryTransformationEngine::ExecuteCommand() -- [%X]", commandValue );

	Command command{ commandValue };

	m_errorFlags = 0;

	const int sf = command.sf ? 12 : 0;
	const bool lm = command.lm;

	switch ( static_cast<Opcode>( command.opcode ) )
	{
		case Opcode::PerspectiveTransformationSingle:
			DoPerspectiveTransformation( m_vectors[ 0 ], sf );
			break;

		case Opcode::PerspectiveTransformationTriple:
			DoPerspectiveTransformation( m_vectors[ 0 ], sf );
			DoPerspectiveTransformation( m_vectors[ 1 ], sf );
			DoPerspectiveTransformation( m_vectors[ 2 ], sf );
			break;

		case Opcode::NormalClipping:
		{
			auto& sxy0 = m_screenXYFifo[ 0 ];
			auto& sxy1 = m_screenXYFifo[ 1 ];
			auto& sxy2 = m_screenXYFifo[ 2 ];
			// cross product
			SetMAC<0>( ( sxy0.x * sxy1.y + sxy1.x * sxy2.y + sxy2.x * sxy0.y ) - ( sxy0.x * sxy2.y + sxy1.x * sxy0.y + sxy2.x * sxy1.y ) );
			break;
		}

		case Opcode::Average3Z:
			CalculateAverageZ( 3, m_zScaleFactor3 );
			break;

		case Opcode::Average4Z:
			CalculateAverageZ( 4, m_zScaleFactor4 );
			break;

		case Opcode::NormalColorDepthCueSingle:
			DoNormalColor<false, true>( m_vectors[ 0 ], sf, lm );
			break;

		case Opcode::NormalColorDepthCueTriple:
			DoNormalColor<false, true>( m_vectors[ 0 ], sf, lm );
			DoNormalColor<false, true>( m_vectors[ 1 ], sf, lm );
			DoNormalColor<false, true>( m_vectors[ 2 ], sf, lm );
			break;

		case Opcode::NormalColorSingle:
			DoNormalColor<false, false>( m_vectors[ 0 ], sf, lm );
			break;

		case Opcode::NormalColorTriple:
			DoNormalColor<false, false>( m_vectors[ 0 ], sf, lm );
			DoNormalColor<false, false>( m_vectors[ 1 ], sf, lm );
			DoNormalColor<false, false>( m_vectors[ 2 ], sf, lm );
			break;

		case Opcode::NormalColorColorSingle:
			DoNormalColor<true, false>( m_vectors[ 0 ], sf, lm );
			break;

		case Opcode::NormalColorColorTriple:
			DoNormalColor<true, false>( m_vectors[ 0 ], sf, lm );
			DoNormalColor<true, false>( m_vectors[ 1 ], sf, lm );
			DoNormalColor<true, false>( m_vectors[ 2 ], sf, lm );
			break;

		default:
			dbBreak(); // TODO
			break;
	}

	if ( m_errorFlags & ErrorFlag::ErrorMask )
		m_errorFlags |= ErrorFlag::Error;
}

void GeometryTransformationEngine::DoPerspectiveTransformation( const Vector16& vector, int shiftAmount ) noexcept
{
	// perspective transformation ignores lm bit

	Transform( m_rotation, vector, m_translation, shiftAmount, false );

	PushScreenZ( m_mac123.z >> ( 12 - shiftAmount ) );

	// TODO: use Unsigned Newton-Raphson (UNR) algorithm
	int64_t temp = ( ( ( m_projectionPlaneDistance * 0x20000 ) / m_screenZFifo.back() ) + 1 ) / 2;
	if ( temp > 0x1ffff || temp == 0 )
	{
		temp = 0x1ffff;
		m_errorFlags |= ErrorFlag::DivideOverflow;
	}

	const int32_t screenX = static_cast<int32_t>( ( temp * m_ir123.x + m_screenOffset.x ) / 0x10000 );
	const int32_t screenY = static_cast<int32_t>( ( temp * m_ir123.y + m_screenOffset.y ) / 0x10000 );
	PushScreenXY( screenX, screenY );

	SetMAC<0>( temp * m_depthQueueParamA + m_depthQueueParamB );
	SetIR<0>( m_mac0 / 0x1000, true );
}

template <bool Color, bool DepthCue>
void GeometryTransformationEngine::DoNormalColor( const Vector16& normal, int shiftAmount, bool lm ) noexcept
{
	Transform( m_lightMatrix, normal, shiftAmount, lm );

	Transform( m_colorMatrix, m_ir123, m_backgroundColor, shiftAmount, lm );

	if constexpr ( DepthCue || Color )
	{
		SetMAC<1>( ( m_color[ 0 ] * m_ir123[ 0 ] ) << 4 );
		SetMAC<2>( ( m_color[ 1 ] * m_ir123[ 1 ] ) << 4 );
		SetMAC<3>( ( m_color[ 2 ] * m_ir123[ 2 ] ) << 4 );
	}

	if constexpr ( DepthCue )
	{
		SetMAC<1>( m_mac123[ 0 ] + ( m_farColor[ 0 ] - m_mac123[ 0 ] ) * m_ir0 );
		SetMAC<2>( m_mac123[ 1 ] + ( m_farColor[ 1 ] - m_mac123[ 1 ] ) * m_ir0 );
		SetMAC<3>( m_mac123[ 2 ] + ( m_farColor[ 2 ] - m_mac123[ 2 ] ) * m_ir0 );
	}

	if constexpr ( DepthCue || Color )
	{
		SetMAC<1>( m_mac123[ 0 ], shiftAmount );
		SetMAC<2>( m_mac123[ 1 ], shiftAmount );
		SetMAC<3>( m_mac123[ 2 ], shiftAmount );
	}

	PushColor( m_mac123.x / 16, m_mac123.y / 16, m_mac123.z / 16 );

	// TODO: not sure is lm is fixed here
	SetIR<1>( m_mac123[ 0 ], lm );
	SetIR<2>( m_mac123[ 1 ], lm );
	SetIR<3>( m_mac123[ 2 ], lm );
}

}