#pragma once

#include <Math/Color.h>
#include <Math/Matrix.h>
#include <Math/Vector.h>

#include <array>

namespace PSX
{

 // aka GTE aka COP2
class GeometryTransformationEngine
{
public:
	enum class Register : uint32_t
	{
		// data registers

		// vector 0
		VXY0, VZ0,

		// vector 1
		VXY1, VZ1,

		// vector 2
		VXY2, VZ2,

		ColorCode,

		OrderTableAvgZ,

		// 16bit accumulators
		IR0, IR1, IR2, IR3,

		// screen XY coordinate FIFO
		SXY0, SXY1, SXY2, SXYP,

		// screen Z coordinate FIFO
		SZ0, SZ1, SZ2, SZ3,

		// color CRGB code/color FIFO
		RGB0, RGB1, RGB2,

		Prohibited,

		// 32bit math accumulators
		MAC0, MAC1, MAC2, MAC3,

		// convert 48bit RGB color to 15bit
		ColorConversionInput, ColorConversionOutput,

		// count leading zero/ones (sign bits)
		LeadingBitsSource, LeadingBitsResult,

		// control registers

		// rotation matrix (3x3)
		RT11RT12, RT13RT21, RT22RT23, RT31RT32, RT33,

		// translation vector
		TranslationX, TranslationY, TranslationZ,

		// light source matrix (3x3)
		L11L12, L13L21, L22L23, L31L32, L33,

		// background color
		BackgroundRed, BackgroundGreen, BackgroundBlue,

		// light color matrix source
		LR1LR2, LR3LG1, LG2LG3, LB1LB2, LB3,

		// far color
		FarColorRed, FarColorGreen, FarColorBlue,

		// screen offset
		ScreenOffsetX, ScreenOffsetY,

		// projection plane distance
		ProjectionPlaneDistance,

		// depth queing param A (coeff)
		DepthQueueA,

		// depth queing param B (offset)
		DepthQueueB,

		// average Z scale factors
		ZScaleFactor3, ZScaleFactor4,

		// calculation errors
		ErrorFlags
	};
	static_assert( static_cast<uint32_t>( Register::ErrorFlags ) == 63 );

	struct ErorFlag
	{
		enum : uint32_t
		{
			IR0Saturated = 1 << 12,

			SY2Saturated = 1 << 13,
			SX2Saturated = 1 << 14,

			MAC0OverflowNegative = 1 << 15,
			MAC0OverflowPositive = 1 << 16,

			DivideOverflow = 1 << 17,

			SZ3OrOTZSaturated = 1 << 18,

			ColorFifoBSaturated = 1 << 19,
			ColorFifoGSaturated = 1 << 20,
			ColorFifoRSaturated = 1 << 21,

			IR3Saturated = 1 << 22,
			IR2Saturated = 1 << 23,
			IR1Saturated = 1 << 24,

			MAC3OverflowNegative = 1 << 25,
			MAC2OverflowNegative = 1 << 26,
			MAC1OverflowNegative = 1 << 27,

			MAC3OverflowPositive = 1 << 28,
			MAC2OverflowPositive = 1 << 29,
			MAC1OverflowPositive = 1 << 30,

			WriteMask = 0x7ffff000
		};
	};

	enum class Opcode
	{
		PerspectiveTransformationSingle = 0x01, // RTPS
		NormalClipping = 0x06, // NCLIP
		OuterProduct = 0x0c, // OP(sf) outer product of 2 vectors
		DepthCueingSingle = 0x10, // DPCS
		InterpolateFarColor = 0x11, // INTPL interpolation of a vector and far color vector
		TransformVector = 0x12, // MVMVA multiply vector by matrix and add translation vector
		NormalColorDepthCueSingle = 0x13, // NCDS normal color depth cue single vector
		ColorDepthCue = 0x14, // CDP
		NormalColorDepthCueTriple = 0x16, // NCDT normal color depth cue triple vectors
		NormalColorColorSingle = 0x1b, // NCCS normal color color single vector
		ColorColor = 0x1c, // CC
		NormalColorSingle = 0x1e, // NCS
		NormalColorTriple = 0x20, // NCT
		SquareIR = 0x28, // SQR(sf)5 square of vector IR
		DepthCueColorLight = 0x29, // DCPL
		DepthCueingTriple = 0x2a, // DPCT
		Average3Z = 0x2d, // AVSZ3 average of 3 z values
		Average4Z = 0x2e, // AVSZ4 average of 4 z values
		PerspectiveTransformationTriple = 0x30, // RTPT
		GeneralInterpolation = 0x3d, // GPF(sf)5
		GeneralInterpolationBase = 0x3e, // GPL(sf)5
		NormalColorColorTriple = 0x3f // normal color color triple vector
	};

	void Reset();

	uint32_t Read( uint32_t index ) const noexcept;

	void Write( uint32_t index, uint32_t value ) noexcept;

	uint32_t ReadControl( uint32_t index ) const noexcept
	{
		return Read( index + 32 );
	}

	void WriteControl( uint32_t index, uint32_t value ) noexcept
	{
		Write( index + 32, value );
	}

	void ExecuteCommand( uint32_t command ) noexcept;

private:
	void DoPerspectiveTransformation( const Math::Vector3<int16_t>& vector, bool shiftFraction ) noexcept;

private:
	using Matrix = Math::Matrix<int16_t, 3, 3>;

	// signed 16bit
	std::array<Math::Vector3<int16_t>, 3> m_vectors;

	Math::ColorRGB<uint8_t> m_color;
	uint8_t m_code;

	uint16_t m_orderTableAvgZ;

	// signed 3bit integer 12bit fraction?
	int16_t m_ir0;

	// signed 16bit
	Math::Vector3<int16_t> m_ir123;

	// TODO: screen XY coordinate FIFOs
	std::array<Math::Vector2<int16_t>, 3> m_screenXYFifo;

	// TODO: screen Z coordinate FIFOs
	std::array<uint16_t, 4> m_screenZFifo;

	// TODO: color CRGB code/color FIFOs
	std::array<uint32_t, 3> m_colorCodeFifo;

	// signed 32 bit
	int32_t m_mac0;
	Math::Vector3<int32_t> m_mac123;

	// convert rgb color between 48bit and 15bit
	Math::ColorRGB<uint8_t> m_colorConversion; // 5bits per component. R/W as uint16_t

	// count leading zeroes/ones
	uint32_t m_leadingBitsSource; // R/W
	uint32_t m_leadingBitsResult; // R

	// signed 3bit integer 12bit fraction
	Matrix m_rotation;

	// signed 31bit integer
	Math::Vector3<int32_t> m_translation;

	// signed 3bit integer 12bit fraction
	Matrix m_lightSource;

	// signed 19bit integer 12bit fraction
	Math::ColorRGB<int32_t> m_backgroundColor;

	// signed 3bit integer 12bit fraction
	Matrix m_lightColor;

	// signed 27bit integer 4bit fraction
	Math::ColorRGB<int32_t> m_farColor;

	// signed 15bit integer 16bit fraction
	Math::Vector2<int32_t> m_screenOffset;

	// unsigned 16bit integer (but it gets sign expanded when read as 32bit), H register
	uint16_t m_projectionPlaneDistance;

	// signed 7bit integer 8bit fraction
	int16_t m_depthQueueParamA;

	// signed 7bit integer 24bit fraction?
	int32_t m_depthQueueParamB;
	
	// average Z scale factors
	// signed 3bit integer 12bit fraction?
	int16_t m_zScaleFactor3;
	int16_t m_zScaleFactor4;

	uint32_t m_errorFlags;
};

}