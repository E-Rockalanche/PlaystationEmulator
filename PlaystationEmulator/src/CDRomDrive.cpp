#include "CDRomDrive.h"

namespace PSX
{

namespace
{

struct RequestRegister
{
	enum : uint8_t
	{
		WantCommand = 1u << 5,
		WantData = 1u << 7
	};
};

struct InterruptFlag
{
	enum : uint8_t
	{
		Response = 0x7u,
		Unknown = 1u << 3,
		CommandStart = 1u << 4, // INT10h Command Start (when INT10h requested via 1F801803h.Index0.Bit5)
		AlwaysOne = 0x7u << 5,
		ResetParameterFifo = 1u << 6
	};
};

struct InterruptResponse
{
	enum : uint8_t
	{
		None = 0x00,
		DataReport = 0x01,
		Second = 0x02,
		First = 0x03,
		DataEnd = 0x04,
		ErrorCode = 0x05,

		// command start can be or'd with the above responses
		CommandStart = 0x10
	};
};

struct AudioVolumeApply
{
	enum : uint8_t
	{
		MuteADPCM = 1,
		ChangeAudioVolume = 1 << 5
	};
};

enum class ControllerCommand : uint8_t
{
	GetStat = 0x01,
	SetLoc = 0x02, // amm, ass, asect
	Play = 0x03, // track
	Forward = 0x04,
	Backward = 0x05,
	ReadN = 0x06,
	MotorOn = 0x07,
	Stop = 0x08,
	Pause = 0x09,
	Init = 0x0a,
	Mute = 0x0b,
	Demute = 0x0c,
	SetFilter = 0x0d, // file, channel
	SetMode = 0x0e, // mode
	GetParam = 0x0f,
	GetLocL = 0x10,
	GetLocP = 0x11,
	SetSession = 0x12, // session
	GetTN = 0x13,
	GetTD = 0x14, // track (BCD)
	SeekL = 0x15,
	SeekP = 0x16,

	Test = 0x19, // sub_function
	GetID = 0x1a,
	ReadS = 0x1b,
	ResetDrive = 0x1c,
	GetQ = 0x1d, // adr, point
	ReadTOC = 0x1e,
	VideoCD = 0x1f, // sub, a, b, c, d, e

	Secret1 = 0x50,
	Secret2 = 0x51, // "Licensed by"
	Secret3 = 0x52, // "Sony"
	Secret4 = 0x53, // "Computer"
	Secret5 = 0x54, // "Entertainment"
	Secret6 = 0x55, // "<region>"
	Secret7 = 0x56,
	SecretLock = 0x57,

	// 0x58-0x5f crashes the HC05 (jumps into a data area)
};

enum class TestFunction : uint8_t
{
	ForceMotorClockwise = 0x00,
	ForceMotorAnticlockwise = 0x01,
	ForceMotorAnticlockwise2 = 0x02,
	ForceMotorOff = 0x03,
	StartSCEx = 0x04,
	StopSCEx = 0x05,
	AdjustRamBalance = 0x06,
	AdjustRamGain = 0x07,
	AdjustRamBalanceOnly = 0x08,

	ForceMotorAnticlockwise3 = 0x10,
	MoveLensUp = 0x11,
	MoveLensDown = 0x12,
	MoveLensOutward = 0x13,
	MoveLensInward = 0x14,
	MoveLensOutInMotorOff = 0x15,

	ForceMotorClockwise2 = 0x17,
	ForceMotorAnticlockwise4 = 0x18,

	GetVersion = 0x20,
	GetDriveSwitches = 0x21,
	GetRegionId = 0x22,
	GetChipServoAmpId = 0x23,
	GetChipSignalProcessorId = 0x24,
	GetChipDecoderId = 0x25,

	ServoSignalSend = 0x50,
	ServoSignalSendWithResponse = 0x51,

	HC05SubCpuReadRamAndIoPorts = 0x60,

	DecoderReadOneRegister = 0x71,
	DecoderWriteOneRegister = 0x72,
	DecoderReadMultipleRegisters = 0x73,
	DecoderWriteMultipleRegisters = 0x74,
	DecoderGetHostTransferInfo = 0x75,
	DecoderPrepareTransfer = 0x76
};

struct ControllerMode
{
	enum : uint8_t
	{
		CDDA = 1u << 0, // 1=Allow to Read CD-DA Sectors; ignore missing EDC
		AutoPause = 1 << 1, // 1=Auto Pause upon End of Track
		Report = 1u << 2, // 1=Enable Report-Interrupts for Audio Play
		XAFilter = 1u << 3, // 1=Process only XA-ADPCM sectors that match Setfilter
		IgnoreBit = 1u << 4, // 1=Ignore Sector Size and Setloc position
		SectorSize = 1u << 5, // 0=800h=DataOnly, 1=924h=WholeSectorExceptSyncBytes
		XAADPCM = 1u << 6, // 0=Off, 1=Send XA-ADPCM sectors to SPU Audio Input
		Speed = 1u << 7 // 0=Normal speed, 1=Double speed
	};
};

struct Status
{
	enum : uint8_t
	{
		Error = 1u,
		SpindleMotor = 1u << 1,
		SeekError = 1u << 2,
		IdError = 1u << 3,
		ShellOpen = 1u << 4, // 1=is/was open

		// only one of these bits can be set at a time
		Read = 1u << 5,
		Seek = 1u << 6,
		Play = 1u << 7
	};
};

struct ErrorCode
{
	enum : uint8_t
	{
		InvalidSubFunction = 0x10,
		WrongNumberOfParameters = 0x20,
		InvalidCommand = 0x40,
		CannotRespondYet = 0x80,
		SeekFailed = 0x04,
		DriveDoorOpened = 0x08
	};
};

}

void CDRomDrive::Reset()
{
	m_index = 0;
	m_interruptEnable = 0;
	m_interruptFlags = 0;
	m_status = 0;
	m_wantCommand = false;
	m_wantData = false;
	m_muteADPCM = true;
	m_motorOn = false;
}

uint8_t CDRomDrive::Read( uint32_t index ) noexcept
{
	dbExpects( m_index < 4 );

	switch ( index )
	{
		case 0:
		{
			// probably will never need command/parameter transmission busy flag
			// TODO: XA_ADPCM fifo empty
			return static_cast<uint8_t>( m_index |
				( m_parameterBuffer.Empty() << 3 ) |
				( !m_parameterBuffer.Full() << 4 ) |
				( !m_responseBuffer.Empty() << 5 ) |
				( !m_dataBuffer.Empty() << 6 ) );
		}

		case 1: // response FIFO (all indices)
			// TODO: when reading past last response, buffer is padded with 0s to the end of the buffer, and then restarts at the first byte
			return m_responseBuffer.Pop();

		case 2: // data FIFO (all indices) 8 or 16 bit
			dbBreakMessage( "use ReadDataFifo() function" );
			return 0;

		case 3:
			switch ( m_index )
			{
				case 0:
				case 2:
					// interrupt enable
					dbLog( "read CDROM interrupt enable" );
					return m_interruptEnable;

				case 1:
				case 3:
					// interrupt flag
					dbLog( "read CDROM interrupt flag" );
					return m_interruptFlags;
			}
			break;
	}

	dbBreak();
	return 0;
}


void CDRomDrive::Write( uint32_t index, uint8_t value ) noexcept
{
	dbExpects( m_index < 4 );

	switch ( index )
	{
		case 0:
			m_index = value % 4;
			break;

		case 1:
			switch ( m_index )
			{
				case 0: // command register
					ExecuteCommand( value );
					break;

				case 1: // sound map data out
					dbLog( "write CDROM sound map data out [%X]", value );
					break;

				case 2: // sound map coding info
					dbLog( "write CDROM sound map coding info [%X]", value );
					break;

				case 3: // audio volume for right-cd-out to right-spu-input
					dbLog( "write CDROM right-cd-out to right-spu-input [%X]", value );
					break;
			}
			break;

		case 2:
			switch ( m_index )
			{
				case 0: // parameter fifo
					m_parameterBuffer.Push( value );
					break;

				case 1: // interrupt enable
					dbLog( "write CDROM interrupt enable [%X]", value );
					m_interruptEnable = value;
					break;

				case 2: // left-cd-out to left-spu-input
					dbLog( "write CDROM left-cd-out to left-spu-input [%X]", value );
					break;

				case 3: // right-cd-out to left-cd-input
					dbLog( "write CDROM right-cd-out to left-cd-input [%X]", value );
					break;
			}
			break;

		case 3:
			switch ( m_index )
			{
				case 0: // request register
				{
					dbLog( "write CDROM request [%X]", value );
					m_wantCommand = value & RequestRegister::WantCommand;
					m_wantData = value & RequestRegister::WantData;
					break;
				}

				case 1: // ack interrupt flags
				{
					dbLog( "write CDROM interrupt flag [%X]", value );
					m_interruptFlags = ( m_interruptFlags & ~value ) | InterruptFlag::AlwaysOne;

					if ( value & InterruptFlag::ResetParameterFifo )
						m_parameterBuffer.Reset(); // TODO: does the buffer get reset anytime there is an ACK?

					// TODO: execute pending command
					break;
				}

				case 2: // audio volume for left-cd-out to right-spu-input
					dbLog( "write CDROM left-cd-out to right-spu-input [%X]", value );
					break;

				case 3: // audio volume apply (write bit5=1)
				{
					m_muteADPCM = value & AudioVolumeApply::MuteADPCM;

					if ( value & AudioVolumeApply::ChangeAudioVolume )
						dbLog( "apply audio volume changes" );

					break;
				}
			}
			break;

		default:
			dbBreak();
			break;
	}
}

#define COMMAND_CASE( command ) case ControllerCommand::command:	command();	break

void CDRomDrive::ExecuteCommand( uint8_t command ) noexcept
{
	m_responseBuffer.Reset();

	switch ( static_cast<ControllerCommand>( command ) )
	{
		/*
		COMMAND_CASE( GetStat );
		COMMAND_CASE( SetLoc );
		COMMAND_CASE( Play );
		COMMAND_CASE( Forward );
		COMMAND_CASE( Backward );
		COMMAND_CASE( ReadN );
		COMMAND_CASE( MotorOn );
		COMMAND_CASE( Stop );
		COMMAND_CASE( Pause );
		COMMAND_CASE( Init );
		COMMAND_CASE( Mute );
		COMMAND_CASE( Demute );
		COMMAND_CASE( SetFilter );
		COMMAND_CASE( SetMode );
		COMMAND_CASE( GetParam );
		COMMAND_CASE( GetLocL );
		COMMAND_CASE( GetLocP );
		COMMAND_CASE( SetSession );
		COMMAND_CASE( GetTN );
		COMMAND_CASE( GetTD );
		COMMAND_CASE( SeekL );
		COMMAND_CASE( SeekP );
		*/
		COMMAND_CASE( Test );
		/*
		COMMAND_CASE( GetID );
		COMMAND_CASE( ReadS );
		COMMAND_CASE( ResetDrive );
		COMMAND_CASE( GetQ );
		COMMAND_CASE( ReadTOC );
		COMMAND_CASE( VideoCD );
		COMMAND_CASE( Secret1 );
		COMMAND_CASE( Secret2 );
		COMMAND_CASE( Secret3 );
		COMMAND_CASE( Secret4 );
		COMMAND_CASE( Secret5 );
		COMMAND_CASE( Secret6 );
		COMMAND_CASE( Secret7 );
		COMMAND_CASE( SecretLock );
		*/

		default:
		{
			dbBreakMessage( "Invalid CDROM command" );
			m_responseBuffer.Push( m_status + 1 );
			m_responseBuffer.Push( 0x40 );
			m_interruptFlags = InterruptResponse::ErrorCode;
			break;
		}
	}

	m_parameterBuffer.Reset();
}

#undef COMMAND_CASE

// CDROM control commands

void CDRomDrive::SetFilter() noexcept
{
	m_file = m_parameterBuffer.Pop();
	m_channel = m_parameterBuffer.Pop();
	dbLog( "CDRomDrive::SetFilter() -- file: %X, channel: %X", (uint32_t)m_file, (uint32_t)m_channel );

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;
}

void CDRomDrive::SetMode() noexcept
{
	m_mode = m_parameterBuffer.Pop();
	dbLog( "CDRomDrive::SetMode() -- mode: %u", (uint32_t)m_mode );

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;
}

void CDRomDrive::Init() noexcept
{
	dbLog( "CDRomDrive::Init()" ); // TODO: set mode=0x00, activate drive motor, standby, abort all commands

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	m_responseBuffer.Push( m_status );
	m_queuedInterrupt = InterruptResponse::Second;
}

void CDRomDrive::ResetDrive() noexcept
{
	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;
}

void CDRomDrive::MotorOn() noexcept
{
	dbLog( "CDRomDrive::MotorOn()" );

	if ( m_motorOn )
	{
		m_responseBuffer.Push( m_status );
		m_responseBuffer.Push( ErrorCode::WrongNumberOfParameters );
		m_interruptFlags = InterruptResponse::ErrorCode;
	}
	else
	{
		m_motorOn = true;

		m_responseBuffer.Push( m_status );
		m_interruptFlags = InterruptResponse::First;

		m_responseBuffer.Push( m_status );
		m_queuedInterrupt = InterruptResponse::Second;
	}
}

void CDRomDrive::Stop() noexcept
{
	dbLog( "CDRomDrive::Stop()" );

	stdx::reset_bits<uint8_t>( m_status, Status::Read );
	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	stdx::reset_bits<uint8_t>( m_status, Status::Error );
	m_responseBuffer.Push( m_status );
	m_queuedInterrupt = InterruptResponse::Second;
}

void CDRomDrive::Pause() noexcept
{
	 // abort reading and playing

	dbLog( "CDRomDrive::Pause()" );

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	stdx::reset_bits<uint8_t>( m_status, Status::Read );
	m_responseBuffer.Push( m_status );
	m_queuedInterrupt = InterruptResponse::Second;
}

// CDROM seek commands

void CDRomDrive::SetLoc() noexcept
{
	m_diskMinutes = m_parameterBuffer.Pop();
	m_diskSeconds = m_parameterBuffer.Pop();
	m_diskSector = m_parameterBuffer.Pop();
	dbLog( "CDRomDrive::SetLoc() -- amm: %X, ass: %X, asect: %X", (uint32_t)m_diskMinutes, (uint32_t)m_diskSeconds, (uint32_t)m_diskSector );

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;
}

void CDRomDrive::SeekL() noexcept
{
	dbBreak();
	// TODO: seek to SetLoc location in data mode

	dbLog( "CDRomDrive::SeekL()" );

	m_status = Status::Seek;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	m_responseBuffer.Push( m_status );
	m_queuedInterrupt = InterruptResponse::Second;
}

void CDRomDrive::SeekP() noexcept
{
	dbBreak();
	// TODO: seek to SetLoc location in audio mode

	dbLog( "CDRomDrive::SeekP()" );

	m_status = Status::Seek;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	m_responseBuffer.Push( m_status );
	m_queuedInterrupt = InterruptResponse::Second;
}

void CDRomDrive::SetSession() noexcept
{
	dbBreak();
	// TODO: seek to session

	dbLog( "CDRomDrive::SetSession()" );

	m_status = Status::Seek;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	m_responseBuffer.Push( m_status );
	m_queuedInterrupt = InterruptResponse::Second;
}

// CDROM read commands

void CDRomDrive::ReadN() noexcept
{
	dbBreak();
	// rtead with retry

	dbLog( "CDRomDrive::ReadN()" );

	m_status = Status::Read;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	// load data

	m_responseBuffer.Push( m_status );
	m_queuedInterrupt = InterruptResponse::DataReport;
}

void CDRomDrive::ReadS() noexcept
{
	dbBreak();
	// ready without retry

	dbLog( "CDRomDrive::ReadS()" );

	m_status = Status::Read;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	// load data

	m_responseBuffer.Push( m_status );
	m_queuedInterrupt = InterruptResponse::DataReport;
}

void CDRomDrive::ReadTOC() noexcept
{
	dbLog( "CDRomDrive::ReadTOC()" );

	dbBreak(); // not supported in vC0
}

// CDROM status commands

void CDRomDrive::GetStat() noexcept
{
	dbLog( "CDRomDrive::GetStat()" );

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	stdx::reset_bits<uint8_t>( m_status, Status::ShellOpen );
}

void CDRomDrive::GetParam() noexcept
{
	dbLog( "CDRomDrive::GetParam()" );

	m_responseBuffer.Push( m_status );
	m_responseBuffer.Push( m_mode );
	m_responseBuffer.Push( 0 );
	m_responseBuffer.Push( m_file );
	m_responseBuffer.Push( m_channel );
	m_interruptFlags = InterruptResponse::First;
}

void CDRomDrive::GetLocL() noexcept
{
	dbLog( "CDRomDrive::GetLocL()" );

	m_responseBuffer.Push( m_diskMinutes );
	m_responseBuffer.Push( m_diskSeconds );
	m_responseBuffer.Push( m_diskSector );
	m_responseBuffer.Push( m_mode );
	m_responseBuffer.Push( m_file );
	m_responseBuffer.Push( m_channel );
	m_responseBuffer.Push( 0 ); // sm?
	m_responseBuffer.Push( 0 ); // ci?
	m_interruptFlags = InterruptResponse::First;
}

void CDRomDrive::GetLocP() noexcept
{
	dbLog( "CDRomDrive::GetLocP()" );

	m_responseBuffer.Push( m_track );
	m_responseBuffer.Push( m_trackIndex );
	m_responseBuffer.Push( m_trackMinutes );
	m_responseBuffer.Push( m_trackSeconds );
	m_responseBuffer.Push( m_trackSector );
	m_responseBuffer.Push( m_diskMinutes );
	m_responseBuffer.Push( m_diskSeconds );
	m_responseBuffer.Push( m_diskSector );
	m_interruptFlags = InterruptResponse::First;
}

void CDRomDrive::GetTN() noexcept
{
	dbLog( "CDRomDrive::GetTN()" );

	m_responseBuffer.Push( m_status );
	m_responseBuffer.Push( m_firstTrack );
	m_responseBuffer.Push( m_lastTrack );
	m_interruptFlags = InterruptResponse::First;
}

void CDRomDrive::GetTD() noexcept
{
	const auto track = m_parameterBuffer.Pop();
	dbLog( "CDRomDrive::GetTD() -- track: %X", (uint32_t)track );

	m_responseBuffer.Push( m_status );

	// TODO: return return start of specified track
	m_responseBuffer.Push( 0 );
	m_responseBuffer.Push( 0 );
	m_interruptFlags = InterruptResponse::First;
}

void CDRomDrive::GetQ() noexcept
{
	const auto adr = m_parameterBuffer.Pop();
	const auto point = m_parameterBuffer.Pop();
	dbLog( "CDRomDrive::GetQ() -- adr: %X, point: %X", (uint32_t)adr, (uint32_t)point );

	dbBreak(); // not supported in vC0
}

void CDRomDrive::GetID() noexcept
{
	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	m_responseBuffer.Push( m_status );
	m_responseBuffer.Push( 0x08 );
	m_responseBuffer.Push( 0x40 );
	m_responseBuffer.Push( 0x00 );
	m_responseBuffer.Push( 0x00 );
	m_responseBuffer.Push( 0x00 );
	m_responseBuffer.Push( 0x00 );
	m_responseBuffer.Push( 0x00 );
	m_responseBuffer.Push( 0x00 );
	m_queuedInterrupt = InterruptResponse::ErrorCode;
}

// CDROM audio commands

void CDRomDrive::Mute() noexcept { dbBreak(); }
void CDRomDrive::Demute() noexcept { dbBreak(); }
void CDRomDrive::Play() noexcept { dbBreak(); }
void CDRomDrive::Forward() noexcept { dbBreak(); }
void CDRomDrive::Backward() noexcept { dbBreak(); }

void CDRomDrive::Test() noexcept
{
	const auto subFunction = static_cast<TestFunction>( m_parameterBuffer.Pop() );

	switch ( subFunction )
	{
		case TestFunction::GetVersion:
			m_responseBuffer.Push( 0x94 );
			m_responseBuffer.Push( 0x09 );
			m_responseBuffer.Push( 0x19 );
			m_responseBuffer.Push( 0xc0 );
			m_interruptFlags = InterruptResponse::First;
			break;

		default: // TODO
			dbBreak();
			break;
	}
}

void CDRomDrive::Secret1() noexcept { dbBreak(); }
void CDRomDrive::Secret2() noexcept { dbBreak(); }
void CDRomDrive::Secret3() noexcept { dbBreak(); }
void CDRomDrive::Secret4() noexcept { dbBreak(); }
void CDRomDrive::Secret5() noexcept { dbBreak(); }
void CDRomDrive::Secret6() noexcept { dbBreak(); }
void CDRomDrive::Secret7() noexcept { dbBreak(); }
void CDRomDrive::SecretLock() noexcept { dbBreak(); }

void CDRomDrive::VideoCD() noexcept { dbBreak(); }

}