#include "GeometryTransformationEngine.h"

#include <stdx/assert.h>

#include <algorithm>

namespace PSX
{

namespace
{

template <typename T>
constexpr uint32_t SignExtend( T value ) noexcept
{
	return static_cast<uint32_t>( static_cast<int16_t>( value ) );
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

	m_lightSource = Matrix( 0 );

	m_backgroundColor = { 0 };

	m_lightColor = Matrix( 0 );

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

	dbLog( "GeometryTransformationEngine::Read() -- [%u]", index );

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
		case Register::RT33:		return SignExtend( m_rotation[ 2 ][ 2 ] );

		case Register::TranslationX:	return static_cast<uint32_t>( m_translation.x );
		case Register::TranslationY:	return static_cast<uint32_t>( m_translation.y );
		case Register::TranslationZ:	return static_cast<uint32_t>( m_translation.z );

		case Register::L11L12:	return readMatrixPair( m_lightSource, 0 );
		case Register::L13L21:	return readMatrixPair( m_lightSource, 2 );
		case Register::L22L23:	return readMatrixPair( m_lightSource, 4 );
		case Register::L31L32:	return readMatrixPair( m_lightSource, 6 );
		case Register::L33:		return SignExtend( m_lightSource[ 2 ][ 2 ] );

		case Register::BackgroundRed:	return static_cast<uint32_t>( m_backgroundColor.r );
		case Register::BackgroundGreen:	return static_cast<uint32_t>( m_backgroundColor.g );
		case Register::BackgroundBlue:	return static_cast<uint32_t>( m_backgroundColor.b );

		case Register::LR1LR2:	return readMatrixPair( m_lightColor, 0 );
		case Register::LR3LG1:	return readMatrixPair( m_lightColor, 2 );
		case Register::LG2LG3:	return readMatrixPair( m_lightColor, 4 );
		case Register::LB1LB2:	return readMatrixPair( m_lightColor, 6 );
		case Register::LB3:		return SignExtend( m_lightColor[ 2 ][ 2 ] );

		case Register::FarColorRed:		return static_cast<uint32_t>( m_farColor.r );
		case Register::FarColorGreen:	return static_cast<uint32_t>( m_farColor.g );
		case Register::FarColorBlue:	return static_cast<uint32_t>( m_farColor.b );

		case Register::ScreenOffsetX:	return static_cast<uint32_t>( m_screenOffset.x );
		case Register::ScreenOffsetY:	return static_cast<uint32_t>( m_screenOffset.y );

		case Register::ProjectionPlaneDistance:		return SignExtend( m_projectionPlaneDistance );

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

	dbLog( "GeometryTransformationEngine::Write() -- [%u <- %X]", index, value );

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
			m_screenXYFifo[ 0 ] = m_screenXYFifo[ 1 ];
			m_screenXYFifo[ 1 ] = m_screenXYFifo[ 2 ];
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

		case Register::L11L12:	assignMatrixPair( m_lightSource, 0, value );	break;
		case Register::L13L21:	assignMatrixPair( m_lightSource, 2, value );	break;
		case Register::L22L23:	assignMatrixPair( m_lightSource, 4, value );	break;
		case Register::L31L32:	assignMatrixPair( m_lightSource, 6, value );	break;
		case Register::L33:		m_lightSource[ 2 ][ 2 ] = static_cast<int16_t>( value );	break;

		case Register::BackgroundRed:	m_backgroundColor.r = static_cast<int32_t>( value );	break;
		case Register::BackgroundGreen:	m_backgroundColor.g = static_cast<int32_t>( value );	break;
		case Register::BackgroundBlue:	m_backgroundColor.b = static_cast<int32_t>( value );	break;

		case Register::LR1LR2:	assignMatrixPair( m_lightColor, 0, value );		break;
		case Register::LR3LG1:	assignMatrixPair( m_lightColor, 2, value );		break;
		case Register::LG2LG3:	assignMatrixPair( m_lightColor, 4, value );		break;
		case Register::LB1LB2:	assignMatrixPair( m_lightColor, 6, value );		break;
		case Register::LB3:		m_lightColor[ 2 ][ 2 ] = static_cast<int16_t>( value );		break;

		case Register::FarColorRed:		m_farColor.r = static_cast<int32_t>( value );	break;
		case Register::FarColorGreen:	m_farColor.g = static_cast<int32_t>( value );	break;
		case Register::FarColorBlue:	m_farColor.b = static_cast<int32_t>( value );	break;

		case Register::ScreenOffsetX:	m_screenOffset.x = static_cast<int32_t>( value );	break;
		case Register::ScreenOffsetY:	m_screenOffset.y = static_cast<int32_t>( value );	break;

		case Register::ProjectionPlaneDistance:	m_projectionPlaneDistance = static_cast<uint16_t>( value );		break;

		case Register::DepthQueueA:		m_depthQueueParamA = static_cast<int16_t>( value );		break;
		case Register::DepthQueueB:		m_depthQueueParamB = static_cast<int32_t>( value );		break;

		case Register::ZScaleFactor3:	m_zScaleFactor3 = static_cast<int16_t>( value );	break;
		case Register::ZScaleFactor4:	m_zScaleFactor4 = static_cast<int16_t>( value );	break;

		case Register::ErrorFlags:	m_errorFlags = value;	break;

		default:
			dbBreak(); // TODO
			break;
	}
}

void GeometryTransformationEngine::ExecuteCommand( uint32_t command ) noexcept
{
	dbLog( "GeometryTransformationEngine::ExecuteCommand() -- [%X]", command );

	const auto opcode = static_cast<Opcode>( command & 0x3f );
	const bool shiftFraction = command & ( 1 << 19 );

	auto calcAvgZ = [this]( uint32_t size, uint32_t scale )
	{
		m_mac0 = 0;
		for ( size_t i = 4 - size; i < 4; ++i )
			m_mac0 += m_screenZFifo[ i ];
		m_mac0 *= scale;
		m_orderTableAvgZ = static_cast<uint16_t>( std::clamp( m_mac0 / 0x1000, 0, 0xffff ) );
	};

	switch ( opcode )
	{
		case Opcode::PerspectiveTransformationSingle:
			DoPerspectiveTransformation( m_vectors[ 0 ], shiftFraction );
			break;

		case Opcode::PerspectiveTransformationTriple:
			DoPerspectiveTransformation( m_vectors[ 0 ], shiftFraction );
			DoPerspectiveTransformation( m_vectors[ 1 ], shiftFraction );
			DoPerspectiveTransformation( m_vectors[ 2 ], shiftFraction );
			break;

		case Opcode::NormalClipping:
		{
			auto& sxy0 = m_screenXYFifo[ 0 ];
			auto& sxy1 = m_screenXYFifo[ 1 ];
			auto& sxy2 = m_screenXYFifo[ 2 ];
			// cross product
			m_mac0 = ( sxy0.x * sxy1.y + sxy1.x * sxy2.y + sxy2.x * sxy0.y ) - ( sxy0.x * sxy2.y + sxy1.x * sxy0.y + sxy2.x * sxy1.y );
			break;
		}

		case Opcode::Average3Z:
			calcAvgZ( 3, m_zScaleFactor3 );
			break;

		case Opcode::Average4Z:
			calcAvgZ( 4, m_zScaleFactor4 );
			break;

		default:
			dbBreak(); // TODO
			break;
	}
}

void GeometryTransformationEngine::DoPerspectiveTransformation( const Math::Vector3<int16_t>& vector, bool shiftFraction ) noexcept
{
	auto shiftFifo = []( auto& buffer )
	{
		for ( size_t i = 1; i < buffer.size(); ++i )
			buffer[ i - 1 ] = buffer[ i ];
	};

	shiftFifo( m_screenXYFifo );
	shiftFifo( m_screenZFifo );

	auto dotProduct3 = []( const auto& lhs, const auto& rhs )
	{
		return lhs[ 0 ] * rhs[ 0 ] + lhs[ 1 ] * rhs[ 1 ] + lhs[ 2 ] * rhs[ 2 ];
	};

	const auto shiftAmount = shiftFraction ? 12 : 0;
	for ( size_t i = 0; i < 3; ++i )
		m_mac123[ i ] = ( m_translation[ i ] * 0x1000 + dotProduct3( m_rotation[ i ], vector ) ) >> shiftAmount;

	for ( size_t i = 0; i < 3; ++i )
		m_ir123[ i ] = static_cast<int16_t>( m_mac123[ i ] );

	m_screenZFifo[ 3 ] = static_cast<uint16_t>( m_mac123[ 2 ] >> ( shiftFraction ? 0 : 12 ) );

	auto temp = ( ( m_projectionPlaneDistance * 0x20000 / m_screenZFifo[ 3 ] ) + 1 ) / 2;
	if ( temp > 0x1ffff )
	{
		temp = 0x1ffff;
		m_errorFlags |= ErorFlag::DivideOverflow;
	}

	m_screenXYFifo[ 2 ].x = static_cast<int16_t>( ( temp * m_ir123.x + m_screenOffset.x ) / 0x10000 );
	m_screenXYFifo[ 2 ].y = static_cast<int16_t>( ( temp * m_ir123.y + m_screenOffset.y ) / 0x10000 );
	m_mac0 = temp * m_depthQueueParamA + m_depthQueueParamB;
	m_ir0 = static_cast<int16_t>( m_mac0 / 0x1000 );
}

}