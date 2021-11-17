#include "GTE.h"

#include <stdx/assert.h>
#include <stdx/bit.h>

#include <algorithm>

#define GTE_LOG_COMMANDS false

#define GTE_USE_FAST_DIVISION true

namespace PSX
{

namespace
{

template <typename Buffer, typename T>
inline void PushBack( Buffer& buffer, T value )
{
	for ( size_t i = 1; i < buffer.size(); ++i )
		buffer[ i - 1 ] = buffer[ i ];

	buffer.back() = value;
}

template <typename T>
constexpr uint32_t SignExtend16( T value ) noexcept
{
	static_assert( sizeof( T ) == sizeof( int16_t ) );
	return static_cast<uint32_t>( static_cast<int32_t>( static_cast<int16_t>( value ) ) );
}

template <typename T, typename U>
constexpr int64_t DotProduct3( const T& lhs, const U& rhs )
{
	return int64_t( lhs[ 0 ] ) * int64_t( rhs[ 0 ] ) + int64_t( lhs[ 1 ] ) * int64_t( rhs[ 1 ] ) + int64_t( lhs[ 2 ] ) * int64_t( rhs[ 2 ] );
}

} // namespace

#if GTE_LOG_COMMANDS
#define LogGTE( ... ) Log( __VA_ARGS__ )
#else
#define LogGTE( ... )
#endif

void GTE::Reset()
{
	m_vectors.fill( Math::Vector3<int16_t>{ 0 } );

	m_color = ColorRGBC();

	m_orderTableZ = 0;

	m_ir0 = 0;
	m_ir123 = Vector16{ 0 };

	m_screenXYFifo.fill( Math::Vector2<int16_t>{ 0 } );
	m_screenZFifo.fill( 0 );
	m_colorCodeFifo.fill( ColorRGBC() );

	m_res1 = 0;

	m_mac0 = 0;
	m_mac123 = Vector32{ 0 };

	m_leadingBitsSource = 0;

	m_rotation = Matrix( 0 );

	m_translation = Vector32{ 0 };

	m_lightMatrix = Matrix( 0 );

	m_backgroundColor = Vector32{ 0 };

	m_colorMatrix = Matrix( 0 );

	m_farColor = Vector32{ 0 };

	m_screenOffset = Math::Vector2i{ 0 };

	m_projectionPlaneDistance = 0;

	m_depthQueueParamA = 0;
	m_depthQueueParamB = 0;

	m_zScaleFactor3 = 0;
	m_zScaleFactor4 = 0;

	m_errorFlags = 0;
}

uint32_t GTE::Read( uint32_t index ) const noexcept
{
	dbExpects( index < 64 );

	auto readVXYn = [this]( size_t n ) -> uint32_t
	{
		auto& v = m_vectors[ n ];
		return static_cast<uint16_t>( v.x ) | ( static_cast<uint16_t>( v.y ) << 16 );
	};

	auto readVZn = [this]( size_t n ) -> uint32_t
	{
		return SignExtend16( m_vectors[ n ].z );
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

		case Register::ColorCode:	return m_color.value;

		case Register::OrderTableAvgZ:	return m_orderTableZ;

		case Register::IR0:		return SignExtend16( m_ir0 );
		case Register::IR1:		return SignExtend16( m_ir123.x );
		case Register::IR2:		return SignExtend16( m_ir123.y );
		case Register::IR3:		return SignExtend16( m_ir123.z );

		case Register::SXY0:	return readScreenXYn( 0 );
		case Register::SXY1:	return readScreenXYn( 1 );
		case Register::SXY2:	return readScreenXYn( 2 );
		case Register::SXYP:	return readScreenXYn( 2 ); // mirror of SXY2

		case Register::SZ0:		return m_screenZFifo[ 0 ];
		case Register::SZ1:		return m_screenZFifo[ 1 ];
		case Register::SZ2:		return m_screenZFifo[ 2 ];
		case Register::SZ3:		return m_screenZFifo[ 3 ];

		case Register::RGB0:	return m_colorCodeFifo[ 0 ].value;
		case Register::RGB1:	return m_colorCodeFifo[ 1 ].value;
		case Register::RGB2:	return m_colorCodeFifo[ 2 ].value;

		case Register::Prohibited:	return m_res1;

		case Register::MAC0:	return static_cast<uint32_t>( m_mac0 );
		case Register::MAC1:	return static_cast<uint32_t>( m_mac123.x );
		case Register::MAC2:	return static_cast<uint32_t>( m_mac123.y );
		case Register::MAC3:	return static_cast<uint32_t>( m_mac123.z );

		case Register::ColorConversionInput:
		case Register::ColorConversionOutput:
		{
			const uint32_t r = static_cast<uint32_t>( std::clamp<int16_t>( m_ir123[ 0 ] >> 7, 0x00, 0x1f ) );
			const uint32_t g = static_cast<uint32_t>( std::clamp<int16_t>( m_ir123[ 1 ] >> 7, 0x00, 0x1f ) );
			const uint32_t b = static_cast<uint32_t>( std::clamp<int16_t>( m_ir123[ 2 ] >> 7, 0x00, 0x1f ) );
			return r | ( g << 5 ) | ( b << 10 );
		}

		case Register::LeadingBitsSource:	return m_leadingBitsSource;

		case Register::LeadingBitsResult:
		{
			int result;
			if ( m_leadingBitsSource >= 0 )
				result = stdx::countl_zero( m_leadingBitsSource );
			else
				result = stdx::countl_one( m_leadingBitsSource );

			dbAssert( result != 0 );
			return static_cast<uint32_t>( result );
		}

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

		// hardware bug: H is sign expanded even though it is unsigned
		case Register::ProjectionPlaneDistance:		return SignExtend16( m_projectionPlaneDistance );

		case Register::DepthQueueA:		return SignExtend16( m_depthQueueParamA ); // TODO: is this sign extended?
		case Register::DepthQueueB:		return static_cast<uint32_t>( m_depthQueueParamB );

		case Register::ZScaleFactor3:	return SignExtend16( m_zScaleFactor3 ); // TODO: is this sign extended?
		case Register::ZScaleFactor4:	return SignExtend16( m_zScaleFactor4 ); // TODO: is this sign extended?

		case Register::ErrorFlags:	return m_errorFlags;

		default:
			dbBreak();
			return 0;
	}
}

void GTE::Write( uint32_t index, uint32_t value ) noexcept
{
	dbExpects( index < 64 );

	// writing to registers doesn't trigger overflow flags

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

	auto assignMatrixPair = [this]( Matrix& matrix, size_t elementOffset, uint32_t value )
	{
		matrix.elements[ elementOffset ] = static_cast<int16_t>( value );
		matrix.elements[ elementOffset + 1 ] = static_cast<int16_t>( value >> 16 );
	};

	auto toScreenXY = []( uint32_t value )
	{
		return ScreenXY( static_cast<int16_t>( value ), static_cast<int16_t>( value >> 16 ) );
	};

	switch ( static_cast<Register>( index ) )
	{
		case Register::VXY0:	assignVXYn( 0, value );	break;
		case Register::VXY1:	assignVXYn( 1, value );	break;
		case Register::VXY2:	assignVXYn( 2, value );	break;

		case Register::VZ0:		assignVZn( 0, value );	break;
		case Register::VZ1:		assignVZn( 1, value );	break;
		case Register::VZ2:		assignVZn( 2, value );	break;

		case Register::ColorCode:	m_color.value = value;	break;

		case Register::OrderTableAvgZ:	m_orderTableZ = static_cast<uint16_t>( value );	break;

		case Register::IR0:		m_ir0 = static_cast<int16_t>( value );			break;
		case Register::IR1:		m_ir123[ 0 ] = static_cast<int16_t>( value );	break;
		case Register::IR2:		m_ir123[ 1 ] = static_cast<int16_t>( value );	break;
		case Register::IR3:		m_ir123[ 2 ] = static_cast<int16_t>( value );	break;

		case Register::SXY0:	m_screenXYFifo[ 0 ] = toScreenXY( value );	break;
		case Register::SXY1:	m_screenXYFifo[ 1 ] = toScreenXY( value );	break;
		case Register::SXY2:	m_screenXYFifo[ 2 ] = toScreenXY( value );	break;

		case Register::SXYP:
			PushBack( m_screenXYFifo, toScreenXY( value ) );
			break;

		case Register::SZ0:		m_screenZFifo[ 0 ] = static_cast<uint16_t>( value );	break;
		case Register::SZ1:		m_screenZFifo[ 1 ] = static_cast<uint16_t>( value );	break;
		case Register::SZ2:		m_screenZFifo[ 2 ] = static_cast<uint16_t>( value );	break;
		case Register::SZ3:		m_screenZFifo[ 3 ] = static_cast<uint16_t>( value );	break;

		case Register::RGB0:	m_colorCodeFifo[ 0 ].value = value;		break;
		case Register::RGB1:	m_colorCodeFifo[ 1 ].value = value;		break;
		case Register::RGB2:	m_colorCodeFifo[ 2 ].value = value;		break;

		case Register::Prohibited:	m_res1 = value;	break;

		case Register::MAC0:	m_mac0 = static_cast<int32_t>( value );			break;
		case Register::MAC1:	m_mac123.x = static_cast<int32_t>( value );		break;
		case Register::MAC2:	m_mac123.y = static_cast<int32_t>( value );		break;
		case Register::MAC3:	m_mac123.z = static_cast<int32_t>( value );		break;

		case Register::ColorConversionInput:
		{
			m_ir123[ 0 ] = static_cast<int16_t>( ( value & 0x1f ) * 0x80 );				// red
			m_ir123[ 1 ] = static_cast<int16_t>( ( ( value >> 5 ) & 0x1f ) * 0x80 );	// green
			m_ir123[ 2 ] = static_cast<int16_t>( ( ( value >> 10 ) & 0x1f ) * 0x80 );	// blue
			break;
		}

		case Register::ColorConversionOutput:	break; // read only

		case Register::LeadingBitsSource:	m_leadingBitsSource = static_cast<int32_t>( value );	break;

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

void GTE::ExecuteCommand( uint32_t commandValue ) noexcept
{
	Command command{ commandValue };

	m_errorFlags = 0;

	const int sf = command.sf ? 12 : 0;
	const bool lm = command.lm;

	switch ( static_cast<Opcode>( command.opcode ) )
	{
		case Opcode::RotateTranslatePerspectiveSingle:
			RotateTranslatePerspectiveTransformation( m_vectors[ 0 ], sf );
			break;

		case Opcode::RotateTranslatePerspectiveTriple:
			RotateTranslatePerspectiveTransformation( m_vectors[ 0 ], sf );
			RotateTranslatePerspectiveTransformation( m_vectors[ 1 ], sf );
			RotateTranslatePerspectiveTransformation( m_vectors[ 2 ], sf );
			break;

		case Opcode::NormalClipping:
		{
			// MAC0 =   SX0*SY1 + SX1*SY2 + SX2*SY0 - SX0*SY2 - SX1*SY0 - SX2*SY1
			auto& sxy0 = m_screenXYFifo[ 0 ];
			auto& sxy1 = m_screenXYFifo[ 1 ];
			auto& sxy2 = m_screenXYFifo[ 2 ];

			SetMAC<0>(
				int64_t( sxy0.x ) * int64_t( sxy1.y ) +
				int64_t( sxy1.x ) * int64_t( sxy2.y ) +
				int64_t( sxy2.x ) * int64_t( sxy0.y ) -
				int64_t( sxy0.x ) * int64_t( sxy2.y ) -
				int64_t( sxy1.x ) * int64_t( sxy0.y ) -
				int64_t( sxy2.x ) * int64_t( sxy1.y ) );

			break;
		}

		case Opcode::Average3Z:
		{
			// MAC0 =  ZSF3*(SZ1+SZ2+SZ3)
			const int64_t result = static_cast<int64_t>( m_zScaleFactor3 ) *
				static_cast<int32_t>( m_screenZFifo[ 1 ] + m_screenZFifo[ 2 ] + m_screenZFifo[ 3 ] );

			SetMAC<0>( result );

			// OTZ  =  MAC0/1000h
			SetOrderTableZ( static_cast<int32_t>( result >> 12 ) );
			break;
		}

		case Opcode::Average4Z:
		{
			// MAC0 =  ZSF4*(SZ0+SZ1+SZ2+SZ3)
			const int64_t result = static_cast<int64_t>( m_zScaleFactor4 ) *
				static_cast<int32_t>( m_screenZFifo[ 0 ] + m_screenZFifo[ 1 ] + m_screenZFifo[ 2 ] + m_screenZFifo[ 3 ] );

			SetMAC<0>( result );

			// OTZ  =  MAC0/1000h
			SetOrderTableZ( static_cast<int32_t>( result >> 12 ) );
			break;
		}

		case Opcode::MultiplyVectorMatrixVectorAdd:
			MultiplyVectorMatrixVectorAdd( command );
			break;

		case Opcode::SquareIR:
		{
			// [MAC1, MAC2, MAC3] = [ IR1*IR1, IR2*IR2, IR3*IR3 ] SHR( sf * 12 )
			SetMAC<1>( int64_t( m_ir123.x ) * int64_t( m_ir123.x ), sf );
			SetMAC<2>( int64_t( m_ir123.y ) * int64_t( m_ir123.y ), sf );
			SetMAC<3>( int64_t( m_ir123.z ) * int64_t( m_ir123.z ), sf );

			// [ IR1, IR2, IR3 ] = [ MAC1, MAC2, MAC3 ]; IR1, IR2, IR3 saturated to max 7FFFh
			// lm flag doesn't matter because result should always be positive
			CopyMACToIR( true );
			break;
		}

		case Opcode::OuterProduct:
		{
			// D1,D2,D3 are meant to be the RT11,RT22,RT33 elements of the RT matrix "misused" as vector. lm should be usually zero.
			const int64_t D1 = m_rotation[ 0 ][ 0 ];
			const int64_t D2 = m_rotation[ 1 ][ 1 ];
			const int64_t D3 = m_rotation[ 2 ][ 2 ];
			SetMAC<1>( ( m_ir123.z * D2 ) - ( m_ir123.y * D3 ), sf ); // IR3*D2-IR2*D3
			SetMAC<2>( ( m_ir123.x * D3 ) - ( m_ir123.z * D1 ), sf ); // IR1*D3-IR3*D1
			SetMAC<3>( ( m_ir123.y * D1 ) - ( m_ir123.x * D2 ), sf ); // IR2*D1-IR1*D2
			CopyMACToIR( lm );
			break;
		}

		case Opcode::NormalColorSingle:
			NormalizeColor<false, false, false>( m_vectors[ 0 ], sf, lm );
			break;

		case Opcode::NormalColorTriple:
			NormalizeColor<false, false, false>( m_vectors[ 0 ], sf, lm );
			NormalizeColor<false, false, false>( m_vectors[ 1 ], sf, lm );
			NormalizeColor<false, false, false>( m_vectors[ 2 ], sf, lm );
			break;

		case Opcode::NormalColorColorSingle:
			NormalizeColor<true, false, true>( m_vectors[ 0 ], sf, lm );
			break;

		case Opcode::NormalColorColorTriple:
			NormalizeColor<true, false, true>( m_vectors[ 0 ], sf, lm );
			NormalizeColor<true, false, true>( m_vectors[ 1 ], sf, lm );
			NormalizeColor<true, false, true>( m_vectors[ 2 ], sf, lm );
			break;

		case Opcode::NormalColorDepthCueSingle:
			NormalizeColor<true, true, true>( m_vectors[ 0 ], sf, lm );
			break;

		case Opcode::NormalColorDepthCueTriple:
			NormalizeColor<true, true, true>( m_vectors[ 0 ], sf, lm );
			NormalizeColor<true, true, true>( m_vectors[ 1 ], sf, lm );
			NormalizeColor<true, true, true>( m_vectors[ 2 ], sf, lm );
			break;

		case Opcode::ColorColor:
			Color<false>( sf, lm );
			break;

		case Opcode::ColorDepthCue:
			Color<true>( sf, lm );
			break;

		case Opcode::DepthCueColorLight:
			DepthCue<true, false>( m_color, sf, lm );
			break;

		case Opcode::DepthCueingSingle:
			DepthCue<false, true>( m_color, sf, lm );
			break;

		case Opcode::DepthCueingTriple:
			DepthCue<false, true>( m_colorCodeFifo.front(), sf, lm );
			DepthCue<false, true>( m_colorCodeFifo.front(), sf, lm );
			DepthCue<false, true>( m_colorCodeFifo.front(), sf, lm );
			break;

		case Opcode::InterpolateFarColor:
		{
			// [MAC1,MAC2,MAC3] = [IR1,IR2,IR3] SHL 12
			SetMAC<1>( int64_t( m_ir123.x ) << 12 );
			SetMAC<2>( int64_t( m_ir123.y ) << 12 );
			SetMAC<3>( int64_t( m_ir123.z ) << 12 );

			LerpFarColorWithMAC( sf );
			ShiftMACRight( sf );
			PushColorFromMAC( lm );
			break;
		}

		case Opcode::GeneralInterpolation:
		{
			SetMAC<1>( ( m_ir123[ 0 ] * m_ir0 ), sf );
			SetMAC<2>( ( m_ir123[ 1 ] * m_ir0 ), sf );
			SetMAC<3>( ( m_ir123[ 2 ] * m_ir0 ), sf );
			PushColorFromMAC( lm );
			break;
		}

		case Opcode::GeneralInterpolationBase:
		{
			SetMAC<1>( ( m_ir123[ 0 ] * m_ir0 ) + ( int64_t( m_mac123[ 0 ] ) << sf ), sf );
			SetMAC<2>( ( m_ir123[ 1 ] * m_ir0 ) + ( int64_t( m_mac123[ 1 ] ) << sf ), sf );
			SetMAC<3>( ( m_ir123[ 2 ] * m_ir0 ) + ( int64_t( m_mac123[ 2 ] ) << sf ), sf );
			PushColorFromMAC( lm );
			break;
		}

		default:
			dbBreak(); // invalid command
			break;
	}

	if ( m_errorFlags & ErrorFlag::ErrorMask )
		m_errorFlags |= ErrorFlag::Error;
}

template <size_t Index>
void GTE::SetMAC( int64_t value, int shiftAmount ) noexcept
{
	dbExpects( shiftAmount == 0 || Index != 0 );

	LogGTE( "SetMAC<%i>( %lli, %i )", (int)Index, value, shiftAmount );

	static constexpr int64_t Min = ( Index == 0 ) ? MAC0Min : MAC123Min;
	static constexpr int64_t Max = ( Index == 0 ) ? MAC0Max : MAC123Max;
	if ( value < Min )
	{
		LogGTE( "\tClamping to %lli", Min );

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
		LogGTE( "\tClamping to %lli", Max );

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

	LogGTE( "\tMAC%i = %lli", (int)Index, value );

	if constexpr ( Index == 0 )
		m_mac0 = static_cast<int32_t>( value );
	else
		m_mac123[ Index - 1 ] = static_cast<int32_t>( value );
}

template <size_t Index>
void GTE::SetIR( int32_t value, bool lm ) noexcept
{
	// duckstation lets ir0 be saturated

	LogGTE( "SetIR<%i>( %i, %s )", (int)Index, value, lm ? "true" : "false" );

	static constexpr auto Min = ( Index == 0 ) ? IR0Min : IR123Min;
	static constexpr auto Max = ( Index == 0 ) ? IR0Max : IR123Max;

	const auto minValue = lm ? 0 : Min;
	if ( value < minValue || value > Max )
	{
		value = ( value < minValue ) ? minValue : Max;

		LogGTE( "\tclamping to %i", value );

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

inline void GTE::CopyMACToIR( bool lm ) noexcept
{
	SetIR<1>( m_mac123.x, lm );
	SetIR<2>( m_mac123.y, lm );
	SetIR<3>( m_mac123.z, lm );
}

template <size_t Component>
uint8_t GTE::TruncateRGB( int32_t value ) noexcept
{
	LogGTE( "TruncateRGB<%i>( %i )", (int)Component, value );

	if ( value < ColorMin || value > ColorMax )
	{
		if constexpr ( Component == 0 )
			m_errorFlags |= ErrorFlag::ColorFifoRSaturated;
		if constexpr ( Component == 1 )
			m_errorFlags |= ErrorFlag::ColorFifoGSaturated;
		if constexpr ( Component == 2 )
			m_errorFlags |= ErrorFlag::ColorFifoBSaturated;

		const uint8_t result = ( value < ColorMin ) ? ColorMin : ColorMax;
		LogGTE( "\tclamping to %i", (int)result );

		return result;
	}

	return static_cast<uint8_t>( value );
}

void GTE::PushScreenZ( int32_t value ) noexcept
{
	LogGTE( "PushScreenZ( %i )", value );

	if ( value < ZMin )
	{
		value = ZMin;
		m_errorFlags |= ErrorFlag::SZ3OrOTZSaturated;

		LogGTE( "\tclamping to %i", value );
	}
	else if ( value > ZMax )
	{
		value = ZMax;
		m_errorFlags |= ErrorFlag::SZ3OrOTZSaturated;

		LogGTE( "\tclamping to %i", value );
	}

	PushBack( m_screenZFifo, static_cast<uint16_t>( value ) );
}

void GTE::PushScreenXY( int32_t x, int32_t y ) noexcept
{
	LogGTE( "PushScreenXY( %i, %i )", x, y );

	auto truncate = [this]( int32_t coord, uint32_t errorFlag ) -> int16_t
	{
		if ( coord < ScreenMin )
		{
			LogGTE( "\tclamping %i to %i", coord, ScreenMin );

			m_errorFlags |= errorFlag;
			return ScreenMin;
		}
		else if ( coord > ScreenMax )
		{
			LogGTE( "\tclamping %i to %i", coord, ScreenMax );

			m_errorFlags |= errorFlag;
			return ScreenMax;
		}
		return static_cast<int16_t>( coord );
	};

	PushBack( m_screenXYFifo, ScreenXY{ truncate( x, ErrorFlag::SX2Saturated ), truncate( y, ErrorFlag::SY2Saturated ) } );
}

void GTE::SetOrderTableZ( int32_t z ) noexcept
{
	LogGTE( "SetOrderTableZ( %i )", z );

	if ( z < ZMin )
	{
		z = ZMin;
		m_errorFlags |= ErrorFlag::SZ3OrOTZSaturated;

		LogGTE( "\tclamping to %i", z );
	}
	else if ( z > ZMax )
	{
		z = ZMax;
		m_errorFlags |= ErrorFlag::SZ3OrOTZSaturated;

		LogGTE( "\tclamping to %i", z );
	}
	m_orderTableZ = static_cast<uint16_t>( z );
}

void GTE::Transform( const Matrix& matrix, const Vector16& vector, int shiftAmount, bool lm ) noexcept
{
	LogGTE( "Transform( m, v, %i, %s )", shiftAmount, lm ? "true" : "false" );
	LogGTE( "m = \t%i\t%i\t%i\n\t%i\t%i\t%i\n\t%i\t%i\t%i",
		matrix[ 0 ][ 0 ], matrix[ 0 ][ 1 ], matrix[ 0 ][ 2 ],
		matrix[ 1 ][ 0 ], matrix[ 1 ][ 1 ], matrix[ 1 ][ 2 ],
		matrix[ 2 ][ 0 ], matrix[ 2 ][ 1 ], matrix[ 2 ][ 2 ]
		);
	LogGTE( "v = %i, %i, %i", vector[ 0 ], vector[ 1 ], vector[ 2 ] );

#define MULT( N ) SetMAC<(N)+1>( DotProduct3( matrix[ (N) ], vector ), shiftAmount )
	MULT( 0 );
	MULT( 1 );
	MULT( 2 );
#undef MULT

	LogGTE( "transform result: %i, %i, %i", m_mac123[ 0 ], m_mac123[ 1 ], m_mac123[ 2 ] );

	CopyMACToIR( lm );
}

void GTE::Transform( const Matrix& matrix, const Vector16& vector, const Vector32& translation, int shiftAmount, bool lm ) noexcept
{
	LogGTE( "Transform( m, v, t, %i, %s )", shiftAmount, lm ? "true" : "false" );
	LogGTE( "m = \t%i\t%i\t%i\n\t%i\t%i\t%i\n\t%i\t%i\t%i",
		matrix[ 0 ][ 0 ], matrix[ 0 ][ 1 ], matrix[ 0 ][ 2 ],
		matrix[ 1 ][ 0 ], matrix[ 1 ][ 1 ], matrix[ 1 ][ 2 ],
		matrix[ 2 ][ 0 ], matrix[ 2 ][ 1 ], matrix[ 2 ][ 2 ]
	);
	LogGTE( "v = %i, %i, %i", vector[ 0 ], vector[ 1 ], vector[ 2 ] );
	LogGTE( "t = %i, %i, %i", translation[ 0 ], translation[ 1 ], translation[ 2 ] );

#define MULT_TRANSLATE( N ) SetMAC<(N)+1>( int64_t( translation[ (N) ] ) * int64_t( 0x1000 ) + DotProduct3( matrix[ (N) ], vector ), shiftAmount )
	MULT_TRANSLATE( 0 );
	MULT_TRANSLATE( 1 );
	MULT_TRANSLATE( 2 );
#undef MULT

	LogGTE( "transform result: %i, %i, %i", m_mac123[ 0 ], m_mac123[ 1 ], m_mac123[ 2 ] );

	CopyMACToIR( lm );
}

void GTE::MultiplyColorWithIR( ColorRGBC color ) noexcept
{
	LogGTE( "MultiplyColorWithIR( %08X )", color.value );

	SetMAC<1>( ( int64_t( color.r ) * int64_t( m_ir123.x ) ) << 4 );
	SetMAC<2>( ( int64_t( color.g ) * int64_t( m_ir123.y ) ) << 4 );
	SetMAC<3>( ( int64_t( color.b ) * int64_t( m_ir123.z ) ) << 4 );
}

void GTE::LerpFarColorWithMAC( int shiftAmount ) noexcept
{
	LogGTE( "LerpFarColorWithMAC( %i )", shiftAmount );
	LogGTE( "far color = %u, %u, %u", m_farColor[ 0 ], m_farColor[ 1 ], m_farColor[ 2 ] );

	// [IR1,IR2,IR3] = (([RFC,GFC,BFC] SHL 12) - [MAC1,MAC2,MAC3]) SAR (sf*12)
	// saturated to -8000h..+7FFFh (ie. as if lm=0)
	SetIR<1>( ( ( int64_t( m_farColor[ 0 ] ) << 12 ) - m_mac123[ 0 ] ) >> shiftAmount, false );
	SetIR<2>( ( ( int64_t( m_farColor[ 1 ] ) << 12 ) - m_mac123[ 1 ] ) >> shiftAmount, false );
	SetIR<3>( ( ( int64_t( m_farColor[ 2 ] ) << 12 ) - m_mac123[ 2 ] ) >> shiftAmount, false );

	// [MAC1,MAC2,MAC3] = (([IR1,IR2,IR3] * IR0) + [MAC1,MAC2,MAC3])
	SetMAC<1>( ( m_ir123[ 0 ] * int64_t( m_ir0 ) ) + m_mac123[ 0 ] );
	SetMAC<2>( ( m_ir123[ 1 ] * int64_t( m_ir0 ) ) + m_mac123[ 1 ] );
	SetMAC<3>( ( m_ir123[ 2 ] * int64_t( m_ir0 ) ) + m_mac123[ 2 ] );
}

void GTE::ShiftMACRight( int shiftAmount ) noexcept
{
	LogGTE( "ShiftMACRight( %i )", shiftAmount );

	// [MAC1,MAC2,MAC3] = [MAC1,MAC2,MAC3] SAR (sf*12)
	SetMAC<1>( m_mac123[ 0 ], shiftAmount );
	SetMAC<2>( m_mac123[ 1 ], shiftAmount );
	SetMAC<3>( m_mac123[ 2 ], shiftAmount );
}

void GTE::PushColorFromMAC( bool lm ) noexcept
{
	LogGTE( "PushColorFromMAC( %s )", lm ? "true" : "false" );

	ColorRGBC color;
	color.r = TruncateRGB<0>( m_mac123.x >> 4 );
	color.g = TruncateRGB<1>( m_mac123.y >> 4 );
	color.b = TruncateRGB<2>( m_mac123.z >> 4 );
	color.c = m_color.c;

	LogGTE( "result color = %08X", color.value );

	PushBack( m_colorCodeFifo, color );

	CopyMACToIR( lm );
}

void GTE::RotateTranslatePerspectiveTransformation( const Vector16& vector, int shiftAmount ) noexcept
{
	// perspective transformation ignores lm bit

	LogGTE( "RotateTranslatePerspectiveTransformation" );

	Transform( m_rotation, vector, m_translation, shiftAmount, false );

	PushScreenZ( m_mac123.z >> ( 12 - shiftAmount ) );

#if GTE_USE_FAST_DIVISION
	const int64_t temp = FastDivide( m_projectionPlaneDistance, m_screenZFifo.back() );
#else
	const int64_t temp = UNRDivide( m_projectionPlaneDistance, m_screenZFifo.back() );
#endif

	const int32_t screenX = static_cast<int32_t>( ( temp * m_ir123.x + m_screenOffset.x ) >> 16 );
	const int32_t screenY = static_cast<int32_t>( ( temp * m_ir123.y + m_screenOffset.y ) >> 16 );
	PushScreenXY( screenX, screenY );

	SetMAC<0>( temp * m_depthQueueParamA + m_depthQueueParamB );
	SetIR<0>( m_mac0 >> 12, true );
}

template <bool MultiplyColorIR, bool LerpFarColor, bool ShiftMAC>
void GTE::NormalizeColor( const Vector16& normal, int shiftAmount, bool lm ) noexcept
{
	LogGTE( "NormalizeColor" );

	Transform( m_lightMatrix, normal, shiftAmount, lm );

	Transform( m_colorMatrix, m_ir123, m_backgroundColor, shiftAmount, lm );

	if constexpr ( MultiplyColorIR )
		MultiplyColorWithIR( m_color );

	if constexpr ( LerpFarColor )
		LerpFarColorWithMAC( shiftAmount );

	if constexpr ( ShiftMAC )
		ShiftMACRight( shiftAmount );

	PushColorFromMAC( lm );
}


template <bool LerpFarColor>
void GTE::Color( int shiftAmount, bool lm ) noexcept
{
	LogGTE( "Color" );

	Transform( m_colorMatrix, m_ir123, m_backgroundColor, shiftAmount, lm );

	MultiplyColorWithIR( m_color );

	if constexpr ( LerpFarColor )
		LerpFarColorWithMAC( shiftAmount );

	ShiftMACRight( shiftAmount );

	PushColorFromMAC( lm );
}

template <bool MultiplyColorIR, bool ShiftColorLeft16>
void GTE::DepthCue( ColorRGBC color, int shiftAmount, bool lm ) noexcept
{
	LogGTE( "DepthCue( %08X, %i, %s )", color.value, shiftAmount, lm ? "true" :"false" );

	if constexpr ( MultiplyColorIR )
		MultiplyColorWithIR( color );

	if constexpr ( ShiftColorLeft16 )
	{
		SetMAC<1>( int64_t( color.r ) << 16 );
		SetMAC<2>( int64_t( color.g ) << 16 );
		SetMAC<3>( int64_t( color.b ) << 16 );
	}

	LerpFarColorWithMAC( shiftAmount );
	ShiftMACRight( shiftAmount );
	PushColorFromMAC( lm );
}

void GTE::MultiplyVectorMatrixVectorAdd( Command command ) noexcept
{
	LogGTE( "MultiplyVectorMatrixVectorAdd( %u )", command.value );

	const int shiftAmount = command.sf ? 12 : 0;

	const Matrix* matrix = nullptr;
	switch ( static_cast<MultiplyMatrix>( command.multiplyMatrix ) )
	{
		case MultiplyMatrix::Rotation:	matrix = &m_rotation;		break;
		case MultiplyMatrix::Light:		matrix = &m_lightMatrix;	break;
		case MultiplyMatrix::Color:		matrix = &m_colorMatrix;	break;
		case MultiplyMatrix::Reserved:	dbBreak();	return;
	}

	const Vector16* vector = nullptr;
	switch ( static_cast<MultiplyVector>( command.multiplyVector ) )
	{
		case MultiplyVector::V0:
		case MultiplyVector::V1:
		case MultiplyVector::V2:
			vector = &m_vectors[ command.multiplyVector ];
			break;

		case MultiplyVector::IR:
			vector = &m_ir123;
			break;
	}

	const Vector32* translation = nullptr;
	switch ( static_cast<TranslationVector>( command.translationVector ) )
	{
		case TranslationVector::Translation:		translation = &m_translation;		break;
		case TranslationVector::BackgroundColor:	translation = &m_backgroundColor;	break;
		case TranslationVector::FarColorBugged:		translation = &m_farColor;			break; // TODO: do buggy transformation
		case TranslationVector::None:				translation = nullptr;				break;
	}

	dbAssert( matrix );
	dbAssert( vector );
	if ( translation )
		Transform( *matrix, *vector, *translation, shiftAmount, command.lm );
	else
		Transform( *matrix, *vector, shiftAmount, command.lm );
}

uint32_t GTE::FastDivide( uint32_t lhs, uint32_t rhs ) noexcept
{
	LogGTE( "FastDivide( %u, %u )", lhs, rhs );

	uint32_t result = 0;
	if ( rhs * 2 <= lhs )
	{
		m_errorFlags |= ErrorFlag::DivideOverflow;
		result = 0x1ffff;
	}
	else
	{
		result = std::min<uint32_t>( ( ( ( lhs * 0x20000 ) / rhs ) + 1 ) / 2, 0x1ffff );
	}

	LogGTE( "result = %u", result );
	return result;
}

uint32_t GTE::UNRDivide( uint32_t lhs, uint32_t rhs ) noexcept
{
	LogGTE( "UNRDivide( %u, %u )", lhs, rhs );

	static const std::array<uint8_t, 257> UNRTable
	{
		0xFF,0xFD,0xFB,0xF9,0xF7,0xF5,0xF3,0xF1,0xEF,0xEE,0xEC,0xEA,0xE8,0xE6,0xE4,0xE3,
		0xE1,0xDF,0xDD,0xDC,0xDA,0xD8,0xD6,0xD5,0xD3,0xD1,0xD0,0xCE,0xCD,0xCB,0xC9,0xC8,
		0xC6,0xC5,0xC3,0xC1,0xC0,0xBE,0xBD,0xBB,0xBA,0xB8,0xB7,0xB5,0xB4,0xB2,0xB1,0xB0,
		0xAE,0xAD,0xAB,0xAA,0xA9,0xA7,0xA6,0xA4,0xA3,0xA2,0xA0,0x9F,0x9E,0x9C,0x9B,0x9A, // 0x00..0x3F
		0x99,0x97,0x96,0x95,0x94,0x92,0x91,0x90,0x8F,0x8D,0x8C,0x8B,0x8A,0x89,0x87,0x86,
		0x85,0x84,0x83,0x82,0x81,0x7F,0x7E,0x7D,0x7C,0x7B,0x7A,0x79,0x78,0x77,0x75,0x74,
		0x73,0x72,0x71,0x70,0x6F,0x6E,0x6D,0x6C,0x6B,0x6A,0x69,0x68,0x67,0x66,0x65,0x64,
		0x63,0x62,0x61,0x60,0x5F,0x5E,0x5D,0x5D,0x5C,0x5B,0x5A,0x59,0x58,0x57,0x56,0x55, // 0x40..0x7F
		0x54,0x53,0x53,0x52,0x51,0x50,0x4F,0x4E,0x4D,0x4D,0x4C,0x4B,0x4A,0x49,0x48,0x48,
		0x47,0x46,0x45,0x44,0x43,0x43,0x42,0x41,0x40,0x3F,0x3F,0x3E,0x3D,0x3C,0x3C,0x3B,
		0x3A,0x39,0x39,0x38,0x37,0x36,0x36,0x35,0x34,0x33,0x33,0x32,0x31,0x31,0x30,0x2F,
		0x2E,0x2E,0x2D,0x2C,0x2C,0x2B,0x2A,0x2A,0x29,0x28,0x28,0x27,0x26,0x26,0x25,0x24, // 0x80..0xBF
		0x24,0x23,0x22,0x22,0x21,0x20,0x20,0x1F,0x1E,0x1E,0x1D,0x1D,0x1C,0x1B,0x1B,0x1A,
		0x19,0x19,0x18,0x18,0x17,0x16,0x16,0x15,0x15,0x14,0x14,0x13,0x12,0x12,0x11,0x11,
		0x10,0x0F,0x0F,0x0E,0x0E,0x0D,0x0D,0x0C,0x0C,0x0B,0x0A,0x0A,0x09,0x09,0x08,0x08,
		0x07,0x07,0x06,0x06,0x05,0x05,0x04,0x04,0x03,0x03,0x02,0x02,0x01,0x01,0x00,0x00, // 0xC0..0xFF
		0x00 // < --one extra table entry( for "(d-7FC0h)/80h" = 100h ); -100h
	};

	uint32_t result = 0;
	if ( rhs * 2 <= lhs )
	{
		m_errorFlags |= ErrorFlag::DivideOverflow;
		result = 0x1ffff;
	}
	else
	{
		int shift = stdx::countl_zero( static_cast<uint16_t>( rhs ) );

		const uint32_t n = lhs << shift;
		uint32_t d = rhs << shift;

		const uint32_t index = ( d - 0x7fc0 ) >> 7;
		const uint32_t u = UNRTable[ index ] + 0x101;

		d = ( ( 0x2000080 - ( d * u ) ) >> 8 );
		d = ( ( 0x0000080 + ( d * u ) ) >> 8 );

		result = std::min<uint32_t>( 0x1ffff, ( ( ( n * d ) + 0x8000 ) >> 16 ) );
	}

	LogGTE( "result = %u", result );
	return result;
}

}