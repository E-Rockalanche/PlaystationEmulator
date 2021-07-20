#include "CDRomDrive.h"

#include "CycleScheduler.h"
#include "InterruptControl.h"

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

CDRomDrive::CDRomDrive( InterruptControl& interruptControl, CycleScheduler& cycleScheduler )
	: m_interruptControl{ interruptControl }
	, m_cycleScheduler{ cycleScheduler }
{
	m_cycleScheduler.Register(
		[this]( uint32_t cycles ) { AddCycles( cycles ); },
		[this] { return GetCyclesUntilCommand(); } );
}

void CDRomDrive::Reset()
{
	m_index = 0;
	m_interruptEnable = 0;
	m_interruptFlags = 0;
	m_queuedInterrupt = 0;

	m_pendingCommand = Command::Invalid;
	m_cyclesUntilCommand = InfiniteCycles;
	m_pendingSecondResponseCommand = Command::Invalid;
	m_cyclesUntilSecondResponse = InfiniteCycles;
	m_commandTransferBusy = false;

	m_status = 0;

	m_file = 0;
	m_channel = 0;
	m_mode = 0;

	m_track = 0;
	m_trackIndex = 0;
	m_trackMinutes = 0;
	m_trackSector = 0;
	m_diskMinutes = 0;
	m_diskSeconds = 0;
	m_diskSector = 0;

	m_firstTrack = 0;
	m_lastTrack = 0;

	m_wantCommand = false;
	m_wantData = false;
	m_muteADPCM = true;
	m_motorOn = false;

	m_parameterBuffer.Reset();
	m_responseBuffer.Reset();
	m_dataBuffer.Reset();
}

uint8_t CDRomDrive::Read( uint32_t index ) noexcept
{
	dbExpects( m_index < 4 );

	switch ( index )
	{
		case 0:
		{
			// TODO: XA_ADPCM fifo empty
			const uint8_t status = static_cast<uint8_t>( m_index |
				( m_parameterBuffer.Empty() << 3 ) |
				( !m_parameterBuffer.Full() << 4 ) |
				( !m_responseBuffer.Empty() << 5 ) |
				( !m_dataBuffer.Empty() << 6 ) |
				( m_commandTransferBusy << 7 ) );

			dbLog( "CDRomDrive::Read() -- status [%X]", status );
			return status;
		}

		case 1: // response FIFO (all indices)
			// TODO: when reading past last response, buffer is padded with 0s to the end of the buffer, and then restarts at the first byte
			dbLog( "CDRomDrive::Read() -- response FIFO [%X]", m_responseBuffer.Peek() );
			return m_responseBuffer.Pop();

		case 2: // data FIFO (all indices) 8 or 16 bit
			dbBreak(); // use ReadDataFifo() function
			return 0;

		case 3:
			switch ( m_index )
			{
				case 0:
				case 2:
					// interrupt enable
					dbLog( "CDRomDrive::Read() -- interrupt enable [%X]", m_interruptEnable );
					return m_interruptEnable;

				case 1:
				case 3:
					// interrupt flag
					dbLog( "CDRomDrive::Read() -- interrupt flags [%X]", m_interruptFlags | InterruptFlag::AlwaysOne );
					return m_interruptFlags | InterruptFlag::AlwaysOne;
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
			dbLog( "CDRomDrive::Write() -- index [%u]", value );
			m_index = value % 4;
			break;

		case 1:
			switch ( m_index )
			{
				case 0: // command register
					dbLog( "CDRomDrive::Write() -- send command [%X]", value );
					SendCommand( static_cast<Command>( value ) );
					break;

				case 1: // sound map data out
					dbLog( "CDRomDrive::Write() -- sound map data out [%X]", value );
					break;

				case 2: // sound map coding info
					dbLog( "CDRomDrive::Write() -- sound map coding info [%X]", value );
					break;

				case 3: // audio volume for right-cd-out to right-spu-input
					dbLog( "CDRomDrive::Write() -- right-cd-out to right-spu-input [%X]", value );
					break;
			}
			break;

		case 2:
			switch ( m_index )
			{
				case 0: // parameter fifo
					dbLog( "CDRomDrive::Write() -- paramater [%X]", value );
					m_parameterBuffer.Push( value );
					break;

				case 1: // interrupt enable
					dbLog( "CDRomDrive::Write() -- interrupt enable [%X]", value );
					m_interruptEnable = value;
					CheckInterrupt();
					break;

				case 2: // left-cd-out to left-spu-input
					dbLog( "CDRomDrive::Write() -- left-cd-out to left-spu-input [%X]", value );
					break;

				case 3: // right-cd-out to left-cd-input
					dbLog( "CDRomDrive::Write() -- right-cd-out to left-cd-input [%X]", value );
					break;
			}
			break;

		case 3:
			switch ( m_index )
			{
				case 0: // request register
				{
					dbLog( "CDRomDrive::Write() -- request [%X]", value );
					m_wantCommand = value & RequestRegister::WantCommand;
					m_wantData = value & RequestRegister::WantData;
					break;
				}

				case 1: // ack interrupt flags
				{
					dbLog( "CDRomDrive::Write() -- interrupt flag [%X]", value );
					m_interruptFlags = m_interruptFlags & ~value;

					if ( value & InterruptFlag::ResetParameterFifo )
						m_parameterBuffer.Reset();

					// m_responseBuffer.Reset(); // TODO when first/second responses are broken up

					if ( m_interruptFlags == 0 )
					{
						if ( m_queuedInterrupt != 0 )
						{
							ShiftQueuedInterrupt();
						}
						else
						{
							// TODO: execute pending command when interrupt is acked
						}
					}

					break;
				}

				case 2: // audio volume for left-cd-out to right-spu-input
					dbLog( "CDRomDrive::Write() -- left-cd-out to right-spu-input [%X]", value );
					break;

				case 3: // audio volume apply (write bit5=1)
				{
					dbLog( "CDRomDrive::Write() -- audio volume apply" );
					m_muteADPCM = value & AudioVolumeApply::MuteADPCM;

					if ( value & AudioVolumeApply::ChangeAudioVolume )
						dbLog( "changing audio volume" );

					break;
				}
			}
			break;

		default:
			dbBreak();
			break;
	}
}

void CDRomDrive::AddCycles( uint32_t cycles ) noexcept
{
	auto updateCycles = []( uint32_t& cyclesUntilEvent, uint32_t currentCycles )
	{
		if ( cyclesUntilEvent == InfiniteCycles )
			return false;

		if ( currentCycles >= cyclesUntilEvent )
		{
			cyclesUntilEvent = InfiniteCycles;
			return true;
		}
		else
		{
			cyclesUntilEvent -= currentCycles;
			return false;
		}
	};

	if ( updateCycles( m_cyclesUntilSecondResponse, cycles ) )
	{
		ExecuteSecondResponse( m_pendingSecondResponseCommand );
		m_pendingSecondResponseCommand = Command::Invalid;
	}

	if ( updateCycles( m_cyclesUntilCommand, cycles ) )
	{
		ExecuteCommand( m_pendingCommand );
		m_pendingCommand = Command::Invalid;
	}
}

uint32_t CDRomDrive::GetCyclesUntilCommand() const noexcept
{
	return static_cast<uint32_t>( std::min( m_cyclesUntilCommand, m_cyclesUntilSecondResponse ) );
}

void CDRomDrive::SendCommand( Command command ) noexcept
{
	dbExpects( !m_commandTransferBusy ); // TODO: malfunction if busy
	dbExpects( m_interruptFlags == 0 ); // TODO: command pends until last IRQ is acked
	dbExpects( m_pendingCommand == Command::Invalid );
	dbExpects( m_cyclesUntilCommand == InfiniteCycles );

	m_cycleScheduler.UpdateSubscriberCycles();

	m_pendingCommand = command;
	m_commandTransferBusy = true;
	m_cyclesUntilCommand = (command == Command::Init) ? 0x0013cce : 0x000c4e1; // init command takes longer to respond

	m_cycleScheduler.ScheduleNextSubscriberUpdate();
}

void CDRomDrive::QueueSecondResponse( Command command, int32_t ticks = 0x0004a00 ) noexcept // default ticks value is placeholder
{
	dbExpects( ticks > 0 );
	dbExpects( m_pendingSecondResponseCommand == Command::Invalid );
	dbExpects( m_cyclesUntilSecondResponse == InfiniteCycles );

	m_pendingSecondResponseCommand = command;
	m_cyclesUntilSecondResponse = ticks;

	// we should be in a CycleScheduler update callback
}

void CDRomDrive::CheckInterrupt() noexcept
{
	if ( ( m_interruptFlags & m_interruptEnable ) != 0 )
		m_interruptControl.SetInterrupt( Interrupt::CDRom );
}

void CDRomDrive::ShiftQueuedInterrupt() noexcept
{
	dbExpects( m_interruptFlags == 0 );
	m_interruptFlags = m_queuedInterrupt;
	m_queuedInterrupt = 0;

	m_responseBuffer = m_secondResponseBuffer;

	CheckInterrupt();
}

#define COMMAND_CASE( command ) case Command::command:	command();	break

void CDRomDrive::ExecuteCommand( Command command ) noexcept
{
	dbLog( "CDRomDrive::ExecuteCommand() -- [%X]", command );

	m_commandTransferBusy = false;
	m_responseBuffer.Reset();
	m_responseBuffer.Fill( 0 );

	switch ( command )
	{
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
		COMMAND_CASE( Test );
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

		default:
		{
			dbBreakMessage( "CDRomDrive::ExecuteCommand() -- Invalid command" );
			stdx::set_bits<uint8_t>( m_status, Status::Error );
			m_responseBuffer.Push( 0x40 );
			m_interruptFlags = InterruptResponse::ErrorCode;
			break;
		}
	}

	m_parameterBuffer.Reset();

	dbAssert( m_interruptFlags != InterruptResponse::None ); // there should be a response

	CheckInterrupt();
}

#undef COMMAND_CASE

void CDRomDrive::ExecuteSecondResponse( Command command ) noexcept
{
	dbLog( "CDRomDrive::ExecuteSecondResponse() -- [%X]", command );

	dbAssert( m_queuedInterrupt == 0 ); // cannot queue more than 1 interrupt
	m_secondResponseBuffer.Reset();
	m_secondResponseBuffer.Fill( 0 );

	switch ( command )
	{
		case Command::GetID:
		{
			// TODO
			// return 'no disk' response for now

			m_status = Status::IdError;
			const uint8_t flags = 1 << 6; // disc missing

			m_secondResponseBuffer.Push( m_status );
			m_secondResponseBuffer.Push( flags );
			m_secondResponseBuffer.Push( 0x20 ); // disc type? should be 0 for no disc?
			m_secondResponseBuffer.Push( 0x00 ); // session info?

			m_secondResponseBuffer.Push( 0x00 );
			m_secondResponseBuffer.Push( 0x00 );
			m_secondResponseBuffer.Push( 0x00 );
			m_secondResponseBuffer.Push( 0x00 );
			m_queuedInterrupt = InterruptResponse::ErrorCode;

			/*
			// return 'licensed' response for now
			m_secondResponseBuffer.Push( 0x02 );
			m_secondResponseBuffer.Push( 0x00 );
			m_secondResponseBuffer.Push( 0x20 );
			m_secondResponseBuffer.Push( 0x00 );
			m_secondResponseBuffer.Push( 'S' );
			m_secondResponseBuffer.Push( 'C' );
			m_secondResponseBuffer.Push( 'E' );
			m_secondResponseBuffer.Push( 'A' );
			m_queuedInterrupt = InterruptResponse::Second;
			*/
			break;
		}

		case Command::Init:
		{
			m_secondResponseBuffer.Push( m_status );
			m_queuedInterrupt = InterruptResponse::Second;
			break;
		}

		case Command::MotorOn:
		{
			dbBreak(); // TODO

			m_secondResponseBuffer.Push( m_status );
			m_queuedInterrupt = InterruptResponse::Second;
			break;
		}

		case Command::Stop:
		{
			dbBreak(); // TODO

			stdx::reset_bits<uint8_t>( m_status, Status::Error );
			m_secondResponseBuffer.Push( m_status );
			m_queuedInterrupt = InterruptResponse::Second;
			break;
		}

		case Command::Pause:
		{
			dbBreak(); // TODO

			stdx::reset_bits<uint8_t>( m_status, Status::Read );
			m_secondResponseBuffer.Push( m_status );
			m_queuedInterrupt = InterruptResponse::Second;
			break;
		}

		case Command::SeekL:
		{
			dbBreak(); // TODO

			m_secondResponseBuffer.Push( m_status );
			m_queuedInterrupt = InterruptResponse::Second;
			break;
		}

		case Command::SeekP:
		{
			dbBreak(); // TODO

			m_secondResponseBuffer.Push( m_status );
			m_queuedInterrupt = InterruptResponse::Second;
			break;
		}

		case Command::SetSession:
		{
			dbBreak(); // TODO

			m_secondResponseBuffer.Push( m_status );
			m_queuedInterrupt = InterruptResponse::Second;
			break;
		}

		case Command::ReadN:
		{
			dbBreak(); // TODO

			m_secondResponseBuffer.Push( m_status );
			m_queuedInterrupt = InterruptResponse::DataReport;
			break;
		}

		case Command::ReadS:
		{
			dbBreak(); // TODO

			m_secondResponseBuffer.Push( m_status );
			m_queuedInterrupt = InterruptResponse::DataReport;
			break;
		}

		default:
			dbBreakMessage( "Command %X does not have a second response", command );
			break;
	}

	if ( m_interruptFlags == 0 )
		ShiftQueuedInterrupt();
}

// CDROM control commands

// Automatic ADPCM (CD-ROM XA) filter ignores sectors except those which have the same channel and file numbers in their subheader.
// This is the mechanism used to select which of multiple songs in a single .XA file to play.
void CDRomDrive::SetFilter() noexcept
{
	m_file = m_parameterBuffer.Pop();
	m_channel = m_parameterBuffer.Pop();
	dbLog( "CDRomDrive::SetFilter() -- file: %X, channel: %X", (uint32_t)m_file, (uint32_t)m_channel );

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	dbBreak(); // TODO
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

	m_mode = 0;

	// TODO: activate driver motor, standby

	m_pendingCommand = Command::Invalid;
	m_pendingSecondResponseCommand = Command::Invalid;
	m_cyclesUntilCommand = InfiniteCycles;
	m_cyclesUntilSecondResponse = InfiniteCycles;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	QueueSecondResponse( Command::Init );
}

// Resets the drive controller, reportedly, same as opening and closing the drive door.
// The command executes no matter if/how many parameters are used
void CDRomDrive::ResetDrive() noexcept
{
	dbLog( "CDRomDrive::ResetDrive()" );

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

		QueueSecondResponse( Command::MotorOn );
	}
}

// Stops motor with magnetic brakes (stops within a second or so) (unlike power-off where it'd keep spinning for about 10 seconds),
// and moves the drive head to the begin of the first track
void CDRomDrive::Stop() noexcept
{
	dbLog( "CDRomDrive::Stop()" );

	stdx::reset_bits<uint8_t>( m_status, Status::Read );
	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	QueueSecondResponse( Command::Stop );
}

// Aborts Reading and Playing, the motor is kept spinning, and the drive head maintains the current location within reasonable error
void CDRomDrive::Pause() noexcept
{
	 // abort reading and playing

	dbLog( "CDRomDrive::Pause()" );

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	QueueSecondResponse( Command::Pause );
}

// CDROM seek commands

// Sets the seek target - but without yet starting the seek operation
void CDRomDrive::SetLoc() noexcept
{
	m_diskMinutes = m_parameterBuffer.Pop();
	m_diskSeconds = m_parameterBuffer.Pop();
	m_diskSector = m_parameterBuffer.Pop();
	dbLog( "CDRomDrive::SetLoc() -- amm: %X, ass: %X, asect: %X", (uint32_t)m_diskMinutes, (uint32_t)m_diskSeconds, (uint32_t)m_diskSector );

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;
}

// Seek to Setloc's location in data mode
void CDRomDrive::SeekL() noexcept
{
	dbLog( "CDRomDrive::SeekL()" );

	m_status = Status::Seek;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	QueueSecondResponse( Command::SeekL );
}

// Seek to Setloc's location in audio mode
void CDRomDrive::SeekP() noexcept
{
	dbLog( "CDRomDrive::SeekP()" );

	m_status = Status::Seek;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	QueueSecondResponse( Command::SeekP );
}

// Seeks to session
void CDRomDrive::SetSession() noexcept
{
	dbLog( "CDRomDrive::SetSession()" );

	m_status = Status::Seek;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	QueueSecondResponse( Command::SetSession );
}

// CDROM read commands

// Read with retry. The command responds once with "stat,INT3", and then it's repeatedly sending "stat,INT1 --> datablock",
// that is continued even after a successful read has occured; use the Pause command to terminate the repeated INT1 responses.
void CDRomDrive::ReadN() noexcept
{
	dbLog( "CDRomDrive::ReadN()" );

	m_status = Status::Read;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	QueueSecondResponse( Command::ReadN );
}

// Read without automatic retry. Not sure what that means... does WHAT on errors?
void CDRomDrive::ReadS() noexcept
{
	dbLog( "CDRomDrive::ReadS()" );

	m_status = Status::Read;

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	QueueSecondResponse( Command::ReadS );
}

void CDRomDrive::ReadTOC() noexcept
{
	dbLog( "CDRomDrive::ReadTOC()" );

	dbBreak(); // not supported in vC0
}

// CDROM status commands

// Returns stat (like many other commands), and additionally does reset the shell open flag for subsequent commands
void CDRomDrive::GetStat() noexcept
{
	dbLog( "CDRomDrive::GetStat()" );

	m_responseBuffer.Push( m_status );
	m_interruptFlags = InterruptResponse::First;

	stdx::reset_bits<uint8_t>( m_status, Status::ShellOpen );
}

// Returns stat (see Getstat above), mode (see Setmode), a null byte (always 00h), and file/channel filter values (see Setfilter)
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

// Retrieves 4-byte sector header, plus 4-byte subheader of the current sector.
// GetlocL can be send during active Read commands (but, mind that the GetlocL-INT3-response can't be received until any pending Read-INT1's are acknowledged).
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

	dbBreak(); // TODO
}

// Retrieves 8 bytes of position information from Subchannel Q with ADR=1.
// Mainly intended for displaying the current audio position during Play. All results are in BCD.
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

// Get first track number, and last track number in the TOC of the current Session.
// The number of tracks in the current session can be calculated as (last-first+1).
// The first track number is usually 01h in the first (or only) session,
// and "last track of previous session plus 1" in further sessions.
void CDRomDrive::GetTN() noexcept
{
	dbLog( "CDRomDrive::GetTN()" );

	m_responseBuffer.Push( m_status );
	m_responseBuffer.Push( m_firstTrack );
	m_responseBuffer.Push( m_lastTrack );
	m_interruptFlags = InterruptResponse::First;
}

// For a disk with NN tracks, parameter values 01h..NNh return the start of the specified track,
// parameter value 00h returns the end of the last track, and parameter values bigger than NNh return error code 10h.
// The GetTD values are relative to Index = 1 and are rounded down to second boundaries
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

	QueueSecondResponse( Command::GetID, 0x0004a00 );
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