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

	struct ErrorFlag
	{
		enum : uint32_t
		{
			IR0Saturated = 1 << 12,

			SY2Saturated = 1 << 13,
			SX2Saturated = 1 << 14,

			MAC0Underflow = 1 << 15,
			MAC0Overflow = 1 << 16,

			DivideOverflow = 1 << 17,

			SZ3OrOTZSaturated = 1 << 18,

			ColorFifoBSaturated = 1 << 19,
			ColorFifoGSaturated = 1 << 20,
			ColorFifoRSaturated = 1 << 21,

			IR3Saturated = 1 << 22,
			IR2Saturated = 1 << 23,
			IR1Saturated = 1 << 24,

			MAC3Underflow = 1 << 25,
			MAC2Underflow = 1 << 26,
			MAC1Underflow = 1 << 27,

			MAC3Overflow = 1 << 28,
			MAC2Overflow = 1 << 29,
			MAC1Overflow = 1 << 30,

			Error = 1u << 31, // set if any bit in ErrorMask is set

			ErrorMask = 0x7f87e000,

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

	union Command
	{
		Command( uint32_t v ) : value{ v } {}

		struct
		{
			uint32_t opcode : 6;
			uint32_t : 4;
			uint32_t lm : 1; // saturate ir123 to 0-7fff
			uint32_t : 2;
			uint32_t mvmvaTranslationVector : 2;
			uint32_t mvmvaMultiplyVector : 2;
			uint32_t mvmvaMultiplyMatrix : 2;
			uint32_t sf : 1; // shift fraction
			uint32_t : 12;
		};
		uint32_t value;
	};
	static_assert( sizeof( Command ) == 4 );

	using Matrix = Math::Matrix<int16_t, 3, 3>;
	using Vector16 = Math::Vector3<int16_t>;
	using Vector32 = Math::Vector3<int32_t>;

	static constexpr int64_t MAC0Min = std::numeric_limits<int32_t>::min();
	static constexpr int64_t MAC0Max = std::numeric_limits<int32_t>::max();

	static constexpr int64_t MAC123Min = -( int64_t( 1 ) << 43 );
	static constexpr int64_t MAC123Max = ( int64_t( 1 ) << 43 ) - 1;

	static constexpr int16_t IR0Min = 0x0000;
	static constexpr int16_t IR0Max = 0x1000;

	static constexpr int16_t IR123Min = std::numeric_limits<int16_t>::min(); // or 0 if lm = 1
	static constexpr int16_t IR123Max = std::numeric_limits<int16_t>::max();

	static constexpr uint8_t ColorMin = 0x00u;
	static constexpr uint8_t ColorMax = 0xffu;

	static constexpr uint16_t ZMin = 0u;
	static constexpr uint16_t ZMax = 0xffffu;

	static constexpr int32_t DivideMin = 0;
	static constexpr int32_t DivideMax = 0x1ffff;

	static constexpr int16_t ScreenMin = -0x400;
	static constexpr int16_t ScreenMax = 0x3ff;

private:

	template <size_t Index>
	void SetMAC( int64_t value, int shiftAmount = 0 ) noexcept;

	template <size_t Index>
	void SetIR( int32_t value, bool lm = false ) noexcept;

	template <size_t Index>
	void SetMACAndIR( int64_t value, int shiftAmount, bool lm ) noexcept;

	template <size_t Index>
	uint32_t TruncateRGB( int32_t value ) noexcept;

	void PushScreenZ( int32_t value ) noexcept;

	void PushScreenXY( int32_t x, int32_t y ) noexcept;

	void PushColor( int32_t r, int32_t g, int32_t b ) noexcept;

	void CalculateAverageZ( size_t size, uint32_t scale ) noexcept;

	// stores result in MAC and IR
	void Transform( const Matrix& matrix, const Vector16& vector, int shiftAmount, bool lm ) noexcept;

	// stores result in MAC and IR
	void Transform( const Matrix& matrix, const Vector16& vector, const Vector32& translation, int shiftAmount, bool lm ) noexcept;

	void DoPerspectiveTransformation( const Vector16& vector, int shiftAmount ) noexcept;

	template <bool Color, bool DepthCue>
	void DoNormalColor( const Vector16& vector, int shiftAmount, bool lm ) noexcept;

private:

	// signed 16bit
	std::array<Vector16, 3> m_vectors{};

	Math::ColorRGB<uint8_t> m_color{ 0 };
	uint8_t m_code = 0;

	uint16_t m_orderTableAvgZ = 0;

	// signed 3bit integer 12bit fraction?
	int16_t m_ir0 = 0;

	// signed 16bit
	Vector16 m_ir123{ 0 };

	// TODO: screen XY coordinate FIFOs
	std::array<Math::Vector2<int16_t>, 3> m_screenXYFifo{};

	// TODO: screen Z coordinate FIFOs
	std::array<uint16_t, 4> m_screenZFifo{};

	// TODO: color CRGB code/color FIFOs
	std::array<uint32_t, 3> m_colorCodeFifo{};

	// signed 32 bit
	int32_t m_mac0 = 0;
	Vector32 m_mac123{ 0 };

	// convert rgb color between 48bit and 15bit
	Math::ColorRGB<uint8_t> m_colorConversion{ 0 }; // 5bits per component. R/W as uint16_t

	// count leading zeroes/ones
	uint32_t m_leadingBitsSource = 0; // R/W
	uint32_t m_leadingBitsResult = 0; // R

	// signed 3bit integer 12bit fraction
	Matrix m_rotation = Matrix( 0 );

	// signed 31bit integer
	Vector32 m_translation{ 0 };

	// signed 3bit integer 12bit fraction
	Matrix m_lightMatrix = Matrix( 0 );

	// signed 19bit integer 12bit fraction
	Vector32 m_backgroundColor{ 0 };

	// signed 3bit integer 12bit fraction
	Matrix m_colorMatrix = Matrix( 0 );

	// signed 27bit integer 4bit fraction
	Vector32 m_farColor{ 0 };

	// signed 15bit integer 16bit fraction
	Math::Vector2<int32_t> m_screenOffset{ 0 };

	// unsigned 16bit integer (but it gets sign expanded when read as 32bit), H register
	uint16_t m_projectionPlaneDistance = 0;

	// signed 7bit integer 8bit fraction
	int16_t m_depthQueueParamA = 0;

	// signed 7bit integer 24bit fraction?
	int32_t m_depthQueueParamB = 0;
	
	// average Z scale factors
	// signed 3bit integer 12bit fraction?
	int16_t m_zScaleFactor3 = 0;
	int16_t m_zScaleFactor4 = 0;

	uint32_t m_errorFlags = 0;
};

}