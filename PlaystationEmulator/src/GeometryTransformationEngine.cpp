#include "GeometryTransformationEngine.h"

#include <stdx/assert.h>

namespace PSX
{

void GeometryTransformationEngine::Reset()
{
	m_vector0 = { 0 };
	m_vector1 = { 0 };
	m_vector2 = { 0 };

	m_color = { 0 };
	m_code = 0;

	m_orderTableAvgZ = 0;

	m_ir0 = 0;
	m_ir123 = { 0 };

	// TODO: screen XY coordinate FIFOs

	// TODO: screen Z coordinate FIFOs

	// TODO: color CRGB code/color FIFOs

	m_mac0 = 0;
	m_mac123 = { 0 };

	m_irgb = 0;
	m_orgb = 0;

	m_lzcs = 0;
	m_lzcr = 0;

	m_rotation = { 0 };

	m_translation = { 0 };

	m_lightSource = { 0 };

	m_backgroundColor = { 0 };

	m_lightColorMatrixSource = { 0 };

	m_farColor = { 0 };

	m_screenOffset = { 0 };

	m_projectionPlaneDistance = 0;

	m_depthQueueParamA = 0;
	m_depthQueueParamB = 0;

	m_zsf3 = 0;
	m_zsf4 = 0;

	m_errorFlag = 0;
}

uint32_t GeometryTransformationEngine::Read( uint32_t index ) const noexcept
{
	(void)index;
	return 0;

	/*
	switch ( static_cast<Register>( index ) )
	{
		case Register::VXY0:
			return static_cast<uint32_t>( m_vector0.x | ( m_vector0.y << 16 ) );

		case Register::VZ0:
			return static_cast<uint32_t>( m_vector0.z );

		case Register::VXY1:
			return static_cast<uint32_t>( m_vector1.x | ( m_vector1.y << 16 ) );

		case Register::VZ1:
			return static_cast<uint32_t>( m_vector1.z );

		case Register::VXY2:
			return static_cast<uint32_t>( m_vector2.x | ( m_vector2.y << 16 ) );

		case Register::VZ2:
			return static_cast<uint32_t>( m_vector2.z );

		case Register::ColorCode:
			return Math::ToCode( m_color ) | ( m_code << 24 );

		case Register::OrderTableAvgZ:
			return m_orderTableAvgZ;

		case Register::IR0:
			return m_ir0;

		case Register::IR1:
			return m_ir123.x;

		case Register::IR2:
			return m_ir123.y;

		case Register::IR3:
			return m_ir123.z;

		case Register::SXY0:
		case Register::SXY1:
		case Register::SXY2:
		case Register::SXYP:
			// TODO
			return 0;

		case Register::SZ0:
		case Register::SZ1:
		case Register::SZ2:
		case Register::SZ3:
			// TODO
			return 0;

		case Register::RGB0:
		case Register::RGB1:
		case Register::RGB2:
			// TODO
			return 0;

		case Register::MAC0:
			return m_mac0;

		case Register::MAC1:
			return m_mac123.x;

		case Register::MAC2:
			return m_mac123.y;

		case Register::MAC3:
			return m_mac123.z;

		case Register::IRGB:
			return m_irgb;

		case Register::ORGB:
			return m_orgb;

		case Register::LZCS:
			return m_lzcs;

		case Register::LZCR:
			return m_lzcr;

		case Register::RT11RT12:
			return m_rotation[ 0 ][ 0 ] | ( m_rotation[ 0 ][ 1 ] << 16 );

		case Register::RT13RT21:
			return m_rotation[ 0 ][ 2 ] | ( m_rotation[ 1 ][ 0 ] << 16 );

		case Register::RT22RT23:
			return m_rotation[ 1 ][ 1 ] | ( m_rotation[ 1 ][ 2 ] << 16 );

		case Register::RT31RT32:
			return m_rotation[ 2 ][ 0 ] | ( m_rotation[ 2 ][ 1 ] << 16 );

		case Register::RT33:
			return m_rotation[ 2 ][ 2 ];

		// TODO...
	}
	*/
}

void GeometryTransformationEngine::Write( uint32_t index, uint32_t value ) noexcept
{
	(void)index, value;
	// TODO
}

void GeometryTransformationEngine::ExecuteCommand( uint32_t command ) noexcept
{
	(void)command;
	// const auto opcode = static_cast<Opcode>( command & 0x3f );
}


}