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
		WantCommandInterrupt = 1u << 5,
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

} // namespace

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
	m_secondResponseCommand = Command::Invalid;
	m_cyclesUntilCommand = InfiniteCycles;
	m_cyclesUntilSecondResponse = InfiniteCycles;

	m_status = 0;

	m_file = 0;
	m_channel = 0;
	m_mode = 0;

	m_track = 0;
	m_trackIndex = 0;
	m_trackLocation = {};
	m_seekLocation = {};

	m_currentSector = 0;

	m_firstTrack = 0;
	m_lastTrack = 0;

	m_muteADPCM = true;
	m_motorOn = false;

	m_parameterBuffer.Reset();
	m_responseBuffer.Reset();
	m_secondResponseBuffer.Reset();
	m_dataBuffer.Reset();

	for ( auto& sector : m_sectorBuffers )
	{
		sector.bytes.fill( 0 );
		sector.size = 0;
	}
}

uint8_t CDRomDrive::Read( uint32_t registerIndex ) noexcept
{
	dbExpects( m_index < 4 );

	switch ( registerIndex )
	{
		case 0:
		{
			// TODO: XA_ADPCM fifo empty
			const uint8_t status = static_cast<uint8_t>( m_index |
				( m_parameterBuffer.Empty() << 3 ) |
				( !m_parameterBuffer.Full() << 4 ) |
				( !m_responseBuffer.Empty() << 5 ) |
				( !m_dataBuffer.Empty() << 6 ) |
				( CommandTransferBusy() << 7 ) );

			dbLog( "CDRomDrive::Read() -- status [%X]", status );
			return status;
		}

		case 1: // response FIFO (all indices)
		{
			// TODO: when reading past last response, buffer is padded with 0s to the end of the buffer, and then restarts at the first byte
			const uint8_t value = m_responseBuffer.Pop();
			dbLog( "CDRomDrive::Read() -- response FIFO [%X]", value );
			return value;
		}

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

void CDRomDrive::Write( uint32_t registerIndex, uint8_t value ) noexcept
{
	dbExpects( m_index < 4 );

	switch ( registerIndex )
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
					m_cycleScheduler.UpdateEarly();
					SendCommand( static_cast<Command>( value ) );
					m_cycleScheduler.ScheduleNextUpdate();
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
					m_cycleScheduler.UpdateEarly();
					m_interruptEnable = value;
					CheckInterrupt();
					m_cycleScheduler.ScheduleNextUpdate();
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

					if ( value & RequestRegister::WantData )
					{
						LoadDataFifo();
					}
					else
					{
						dbLog( "\tclearing data FIFO" );
						m_dataBuffer.Clear();
					}
					break;
				}

				case 1: // ack interrupt flags
				{
					dbLog( "CDRomDrive::Write() -- interrupt flag [%X]", value );
					m_cycleScheduler.UpdateEarly();

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
							CheckPendingCommand();
						}
					}

					m_cycleScheduler.ScheduleNextUpdate();

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
	}
}

void CDRomDrive::AddCycles( uint32_t cycles ) noexcept
{
	auto updateCycles = []( uint32_t& cyclesUntilEvent, uint32_t currentCycles )
	{
		if ( cyclesUntilEvent == InfiniteCycles )
			return false;

		dbAssert( currentCycles <= cyclesUntilEvent );
		cyclesUntilEvent -= currentCycles;

		if ( cyclesUntilEvent == 0 )
		{
			cyclesUntilEvent = InfiniteCycles;
			return true;
		}

		return false;
	};

	if ( updateCycles( m_cyclesUntilSecondResponse, cycles ) )
	{
		const auto command = std::exchange( m_secondResponseCommand, Command::Invalid );
		ExecuteSecondResponse( command );
	}

	if ( updateCycles( m_cyclesUntilCommand, cycles ) )
	{
		const auto command = std::exchange( m_pendingCommand, Command::Invalid );
		ExecuteCommand( command );
	}
}

uint32_t CDRomDrive::GetCyclesUntilCommand() const noexcept
{
	return static_cast<uint32_t>( std::min( m_cyclesUntilCommand, m_cyclesUntilSecondResponse ) );
}

void CDRomDrive::SendCommand( Command command ) noexcept
{
	if ( CommandTransferBusy() )
	{
		dbLogWarning( "CDRomDrive::SendCommand() -- command transfer is busy" );
	}

	if ( m_secondResponseCommand != Command::Invalid )
	{
		dbLogWarning( "CDRomDrive::SendCommand() -- cancelling second response [%X]", m_secondResponseCommand );
		m_secondResponseCommand = Command::Invalid;
		m_cyclesUntilSecondResponse = InfiniteCycles;
	}

	m_pendingCommand = command;
	CheckPendingCommand();
}

void CDRomDrive::QueueSecondResponse( Command command, int32_t ticks = 0x0004a00 ) noexcept // default ticks value is placeholder
{
	dbExpects( m_cycleScheduler.IsUpdating() ); // this should only get called in a cycle update callback
	dbExpects( ticks > 0 );
	// dbExpects( m_secondResponseCommand == Command::Invalid );
	// dbExpects( m_cyclesUntilSecondResponse == InfiniteCycles );

	m_secondResponseCommand = command;
	m_cyclesUntilSecondResponse = ticks;
}

void CDRomDrive::CheckPendingCommand() noexcept
{
	// latest command doesn't send until the interrupt are cleared
	if ( m_pendingCommand != Command::Invalid && m_interruptFlags == 0 )
		m_cyclesUntilCommand = GetFirstResponseCycles( m_pendingCommand );
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

void CDRomDrive::AbortCommands() noexcept
{
	m_pendingCommand = Command::Invalid;
	m_secondResponseCommand = Command::Invalid;
	m_cyclesUntilCommand = InfiniteCycles;
	m_cyclesUntilSecondResponse = InfiniteCycles;
}

#define COMMAND_CASE( command ) case Command::command:	command();	break

void CDRomDrive::ExecuteCommand( Command command ) noexcept
{
	dbLog( "CDRomDrive::ExecuteCommand() -- [%X]", command );

	m_responseBuffer.Reset( 0 );

	switch ( command )
	{
		// Control commands

		case Command::SetFilter:
		{
			// Automatic ADPCM (CD-ROM XA) filter ignores sectors except those which have the same channel and file numbers in their subheader.
			// This is the mechanism used to select which of multiple songs in a single .XA file to play.
			m_file = m_parameterBuffer.Pop();
			m_channel = m_parameterBuffer.Pop();
			dbLog( "CDRomDrive::SetFilter -- file: %X, channel: %X", m_file, m_channel );

			SendResponse();

			dbBreak(); // TODO
			break;
		}

		case Command::SetMode:
		{
			m_mode = m_parameterBuffer.Pop();
			dbLog( "CDRomDrive::SetMode() -- mode: %u", (uint32_t)m_mode );

			SendResponse();
			break;
		}

		case Command::Init:
		{
			dbLog( "CDRomDrive::Init" ); // TODO: set mode=0x00, activate drive motor, standby, abort all commands

			m_mode = 0;
			m_motorOn = true;

			// TODO: standby

			AbortCommands();

			SendResponse();
			QueueSecondResponse( Command::Init );
			break;
		}

		case Command::ResetDrive:
		{
			// Resets the drive controller, reportedly, same as opening and closing the drive door.
			// The command executes no matter if/how many parameters are used
			dbLog( "CDRomDrive::ResetDrive" );
			dbBreak(); // TODO
			SendResponse();
			break;
		}

		case Command::MotorOn:
		{
			dbLog( "CDRomDrive::MotorOn" );
			if ( m_motorOn )
			{
				SendError( ErrorCode::WrongNumberOfParameters );
			}
			else
			{
				m_motorOn = true;
				SendResponse();
				QueueSecondResponse( Command::MotorOn );
			}
			break;
		}

		case Command::Stop:
		{
			// Stops motor with magnetic brakes (stops within a second or so) (unlike power-off where it'd keep spinning for about 10 seconds),
			// and moves the drive head to the begin of the first track
			dbLog( "CDRomDrive::Stop" );
			stdx::reset_bits<uint8_t>( m_status, Status::Read | Status::Seek | Status::Play );
			SendResponse();
			const uint32_t stopCycles = m_motorOn ? ( ( m_mode & ControllerMode::DoubleSpeed ) ? 25000000 : 13000000 ) : 7000;
			QueueSecondResponse( Command::Stop, stopCycles );
			break;
		}

		case Command::Pause:
		{
			// Aborts Reading and Playing, the motor is kept spinning, and the drive head maintains the current location within reasonable error
			dbLog( "CDRomDrive::Pause()" );

			switch ( m_secondResponseCommand )
			{
				case Command::ReadN:
				case Command::ReadS:
				case Command::Play:
					m_secondResponseCommand = Command::Invalid;
					m_cyclesUntilSecondResponse = InfiniteCycles;
					break;
			}

			SendResponse();
			QueueSecondResponse( Command::Pause );
			break;
		}

		// Seek commands

		case Command::SetLoc:
		{
			const auto mm = m_parameterBuffer.Pop();
			const auto ss = m_parameterBuffer.Pop();
			const auto sect = m_parameterBuffer.Pop();
			dbLog( "CDRomDrive::SetLoc -- amm: %X, ass: %X, asect: %X", mm, ss, sect );

			if ( IsValidBCDAndLess( mm, CDRom::MinutesPerDiskBCD ) &&
				IsValidBCDAndLess( ss, CDRom::SecondsPerMinuteBCD ) &&
				IsValidBCDAndLess( sect, CDRom::SectorsPerSecondBCD ) )
			{
				// valid arguments
				m_seekLocation = CDRom::Location::FromBCD( mm, ss, sect );
				SendResponse();
			}
			else
			{
				SendError( ErrorCode::InvalidArgument );
			}
			break;
		}

		case Command::SeekL:
		case Command::SeekP:
		{
			dbLog( "CDRomDrive::%s", ( command == Command::SeekL ) ? "SeekL" : "SeekP" );

			if ( m_status & Status::Seek )
				dbLogWarning( "\talready seeking" );

			if ( CanReadDisk() )
			{
				m_status |= Status::Seek;
				SendResponse();
				QueueSecondResponse( command );
			}
			else
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			break;
		}

		case Command::SetSession:
		{
			dbLog( "CDRomDrive::SetSession" );
			m_status |= Status::Seek;
			SendResponse();
			QueueSecondResponse( Command::SetSession );
			break;
		}

		// Read Commands

		case Command::ReadN:
			// Read with retry. The command responds once with "stat,INT3", and then it's repeatedly sending "stat,INT1 --> datablock",
			// that is continued even after a successful read has occured; use the Pause command to terminate the repeated INT1 responses.

		case Command::ReadS:
			// Read without automatic retry. Not sure what that means... does WHAT on errors?
		{
			dbLog( "CDRomDrive::ReadN" );

			if ( m_status & Status::Read )
				dbLog( "\talready reading" );

			m_status |= Status::Read;
			ClearSectorBuffers();
			m_readSectorBuffer = 0;
			m_writeSectorBuffer = 0;
			SendResponse();
			QueueSecondResponse( command, GetReadCycles() );
			break;
		}

		// Status commands

		case Command::GetStat:
		{
			// Returns stat (like many other commands), and additionally does reset the shell open flag for subsequent commands
			SendResponse();
			stdx::reset_bits<uint8_t>( m_status, Status::ShellOpen );
			break;
		}

		case Command::GetParam:
		{
			dbLog( "CDRomDrive::GetParam" );
			SendResponse();
			m_responseBuffer.Push( m_mode );
			m_responseBuffer.Push( 0 );
			m_responseBuffer.Push( m_file );
			m_responseBuffer.Push( m_channel );
			break;
		}

		case Command::GetLocL:
		{
			// Retrieves 4-byte sector header, plus 4-byte subheader of the current sector.
			// GetlocL can be send during active Read commands (but, mind that the GetlocL-INT3-response can't be received until any pending Read-INT1's are acknowledged).
			dbLog( "CDRomDrive::GetLocL" );

			m_responseBuffer.Push( BinaryToBCD( m_seekLocation.minute ) );
			m_responseBuffer.Push( BinaryToBCD( m_seekLocation.second ) );
			m_responseBuffer.Push( BinaryToBCD( m_seekLocation.sector ) );
			m_responseBuffer.Push( m_mode );
			m_responseBuffer.Push( m_file );
			m_responseBuffer.Push( m_channel );
			m_responseBuffer.Push( 0 ); // sm?
			m_responseBuffer.Push( 0 ); // ci?
			m_interruptFlags = InterruptResponse::First;

			dbBreak(); // TODO
			break;
		}

		case Command::GetLocP:
		{
			// Retrieves 8 bytes of position information from Subchannel Q with ADR=1.
			// Mainly intended for displaying the current audio position during Play. All results are in BCD.
			dbLog( "CDRomDrive::GetLocP" );

			m_responseBuffer.Push( m_track );
			m_responseBuffer.Push( m_trackIndex );
			m_responseBuffer.Push( BinaryToBCD( m_trackLocation.minute ) );
			m_responseBuffer.Push( BinaryToBCD( m_trackLocation.second ) );
			m_responseBuffer.Push( BinaryToBCD( m_trackLocation.sector ) );
			m_responseBuffer.Push( BinaryToBCD( m_seekLocation.minute ) );
			m_responseBuffer.Push( BinaryToBCD( m_seekLocation.second ) );
			m_responseBuffer.Push( BinaryToBCD( m_seekLocation.sector ) );
			m_interruptFlags = InterruptResponse::First;
			break;
		}

		case Command::GetTrackNumber:
		{
			// Get first track number, and last track number in the TOC of the current Session.
			// The number of tracks in the current session can be calculated as (last-first+1).
			// The first track number is usually 01h in the first (or only) session,
			// and "last track of previous session plus 1" in further sessions.
			dbLog( "CDRomDrive::GetTrackNumber" );

			SendResponse();
			m_responseBuffer.Push( m_firstTrack );
			m_responseBuffer.Push( m_lastTrack );
			break;
		}

		case Command::GetTD:
		{
			// For a disk with NN tracks, parameter values 01h..NNh return the start of the specified track,
			// parameter value 00h returns the end of the last track, and parameter values bigger than NNh return error code 10h.
			// The GetTD values are relative to Index = 1 and are rounded down to second boundaries
			const auto track = m_parameterBuffer.Pop();
			dbLog( "CDRomDrive::GetTD -- track: %X", (uint32_t)track );

			SendResponse();

			// TODO: return return start of specified track
			m_responseBuffer.Push( 0 );
			m_responseBuffer.Push( 0 );
			break;
		}

		case Command::GetID:
		{
			SendResponse();
			QueueSecondResponse( Command::GetID, 0x0004a00 );
			break;
		}

		// CD audio commands

		case Command::Mute:
		{
			// Turn off audio streaming to SPU (affects both CD-DA and XA-ADPCM).
			// Even when muted, the CDROM controller is internally processing audio sectors( as seen in 1F801800h.Bit2, which works as usually for XA - ADPCM ),
			// muting is just forcing the CD output volume to zero.
			// Mute is used by Dino Crisis 1 to mute noise during modchip detection.
			dbLog( "CDRomDRive::Mute" );
			// TODO
			SendResponse();
			break;
		}

		case Command::Demute:
		{
			// Turn on audio streaming to SPU (affects both CD-DA and XA-ADPCM). The Demute command is needed only if one has formerly used the Mute command
			// (by default, the PSX is demuted after power-up (...and/or after Init command?), and is demuted after cdrom-booting).
			dbLog( "CDRomDRive::Demute" );
			// TODO
			SendResponse();
			break;
		}

		case Command::Play:
		{
			dbLog( "CDRomDRive::Play" );
			SendResponse();
			dbBreak(); // TODO
			break;
		}

		case Command::Forward:
		{
			dbLog( "CDRomDRive::Forward" );
			SendResponse();
			dbBreak(); // TODO
			break;
		}

		case Command::Backward:
		{
			dbLog( "CDRomDRive::Backward" );
			SendResponse();
			dbBreak(); // TODO
			break;
		}

		// Test commands

		case Command::Test:
		{
			dbLog( "CDRomDRive::Test" );
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
			break;
		}

		// Secret unlock commands

		case Command::Secret1:
		case Command::Secret2:
		case Command::Secret3:
		case Command::Secret4:
		case Command::Secret5:
		case Command::Secret6:
		case Command::Secret7:
		case Command::SecretLock:
		{
			dbBreak(); // TODO
			break;
		}

		default:
		{
			dbBreakMessage( "CDRomDrive::ExecuteCommand() -- Invalid command" );
			// stdx::set_bits<uint8_t>( m_status, Status::Error ); I don't think the bits actually get set in m_status
			SendError( ErrorCode::InvalidCommand );
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
	m_secondResponseBuffer.Reset( 0 );

	switch ( command )
	{
		case Command::GetID:
		{
			dbLog( "CDRomDrive::GetID -- second response" );
			if ( CanReadDisk() )
			{
				static const uint8_t LicensedResponse[]{ 0x02, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A' };
				m_secondResponseBuffer.Push( LicensedResponse, 8 );
				m_queuedInterrupt = InterruptResponse::Second;
			}
			else
			{
				static const uint8_t NoDiskResponse[]{ 0x08, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
				m_secondResponseBuffer.Push( NoDiskResponse, 8 );
				m_queuedInterrupt = InterruptResponse::Error;
			}
			break;
		}

		case Command::Init:
		{
			dbLog( "CDRomDrive::Init -- second response" );
			SendSecondResponse();
			break;
		}

		case Command::MotorOn:
		{
			dbLog( "CDRomDrive::MotorOn -- second response" );
			SendSecondResponse();
			break;
		}

		case Command::Stop:
		{
			dbLog( "CDRomDrive::Stop -- second response" );
			dbBreak(); // TODO

			stdx::reset_bits<uint8_t>( m_status, Status::Error );
			SendSecondResponse();
			break;
		}

		case Command::Pause:
		{
			dbLog( "CDRomDrive::Pause -- second response" );
			stdx::reset_bits<uint8_t>( m_status, Status::Read | Status::Play | Status::Seek );
			SendSecondResponse();
			break;
		}

		case Command::SeekL:
		case Command::SeekP:
		{
			dbLog( "CDRomDrive::Seek%c -- second response", ( command == Command::SeekL ) ? 'L' : 'P' );

			stdx::reset_bits<uint8_t>( m_status, Status::Seek );
			if ( CanReadDisk() )
			{
				m_currentSector = m_seekLocation.GetLogicalSector();
				m_cdrom->Seek( m_currentSector );
				SendSecondResponse();
			}
			else
			{
				SendSecondError( ErrorCode::CannotRespondYet );
			}
			break;
		}

		case Command::SetSession:
		{
			dbLog( "CDRomDrive::SetSession -- second response" );

			stdx::reset_bits<uint8_t>( m_status, Status::Seek );
			dbBreak(); // TODO
			SendSecondResponse();
			break;
		}

		case Command::ReadN:
		case Command::ReadS:
		{
			dbLog( "CDRomDrive::Read%c -- second response", ( command == Command::ReadN ) ? 'N' : 'S' );

			stdx::reset_bits<uint8_t>( m_status, Status::Read );

			if ( !CanReadDisk() )
			{
				SendSecondError( ErrorCode::CannotRespondYet );
				break;
			}

			if ( !m_cdrom->ReadSync() )
			{
				dbLogWarning( "CDRomDrive::Read%c -- invalid sector sync", ( command == Command::ReadN ) ? 'N' : 'S' );
				SendSecondError( ErrorCode::SeekFailed ); // TODO: should this happen at the end of seek?
			}

			auto& sector = m_sectorBuffers[ m_writeSectorBuffer ];

			const bool readFullSector = m_mode & ControllerMode::SectorSize;
			const uint32_t readCount = readFullSector ? DataBufferSize : CDRom::DataBytesPerSector;

			if ( !readFullSector )
			{
				// process headers
				const auto header = m_cdrom->ReadHeader();
				switch ( header.mode )
				{
					case 0: // zero filled
						dbBreak();
						break;

					case 1: // original CDROM
						break;

					case 2:
					case 3:
					{
						const auto subHeader = m_cdrom->ReadSubHeader();
						// dbAssert( subHeader.submode & CDRom::SubMode::Data ); //can only read data for now
						break;
					}

					default: // invalid
						dbBreak();
						break;
				}
			}

			m_cdrom->Read( (char*)sector.bytes.data(), readCount );
			sector.size = readCount;

			m_writeSectorBuffer = ( m_writeSectorBuffer + 1 ) % NumSectorBuffers;

			SendSecondResponse( InterruptResponse::ReceivedData );

			// schedule next sector to read
			m_currentSector++;
			m_cdrom->Seek( m_currentSector );

			if ( command == Command::ReadN )
				QueueSecondResponse( command, GetReadCycles() );

			break;
		}

		default:
			dbBreakMessage( "Command %X does not have a second response", command );
			break;
	}

	if ( m_interruptFlags == 0 )
		ShiftQueuedInterrupt();
}

void CDRomDrive::LoadDataFifo() noexcept
{
	dbLog( "CDRomDrive::LoadDataFifo()" );

	dbAssert( m_dataBuffer.Empty() );

	auto& sector = m_sectorBuffers[ m_readSectorBuffer ];

	if ( sector.size > 0 )
	{
		m_dataBuffer.Push( sector.bytes.data(), sector.size );
		sector.size = 0;
	}
	else
	{
		dbLogWarning( "reading from empty sector buffer" );
		m_dataBuffer.Push( sector.bytes.data(), DataBufferSize );
	}

	// the PSX skips all unprocessed sectors and jumps straight to the newest sector
	m_readSectorBuffer = m_writeSectorBuffer;

	auto& nextSector = m_sectorBuffers[ m_readSectorBuffer ];
	if ( nextSector.size > 0 )
	{
		dbLog( "sending additional interrupt for next sector" );
		SendSecondResponse( InterruptResponse::ReceivedData );
		CheckInterrupt();
	}
}

// CDROM control commands

}