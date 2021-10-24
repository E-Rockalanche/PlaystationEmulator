#include "CDRomDrive.h"

#include "EventManager.h"
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

const std::array<uint8_t, 256> CDRomDrive::ExpectedCommandParameters = []
{
	std::array<uint8_t, 256> parameters{};
	parameters[ static_cast<size_t>( CDRomDrive::Command::SetLoc ) ] = 3;
	parameters[ static_cast<size_t>( CDRomDrive::Command::SetFilter ) ] = 2;
	parameters[ static_cast<size_t>( CDRomDrive::Command::SetMode ) ] = 1;
	parameters[ static_cast<size_t>( CDRomDrive::Command::SetSession ) ] = 1;
	parameters[ static_cast<size_t>( CDRomDrive::Command::GetTD ) ] = 1;
	parameters[ static_cast<size_t>( CDRomDrive::Command::Test ) ] = 1;
	return parameters;
}();

CDRomDrive::CDRomDrive( InterruptControl& interruptControl, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
{
	m_commandEvent = eventManager.CreateEvent( "CDRomDrive command event", [this]( cycles_t )
		{
			ExecuteCommand();
		} );

	m_secondResponseEvent = eventManager.CreateEvent( "CDRomDrive second response", [this]( cycles_t )
		{
			ExecuteSecondResponse();
		} );

	m_driveEvent = eventManager.CreateEvent( "CDRomDrive drive event", [this]( cycles_t )
		{
			ExecuteDrive();
		} );
}

CDRomDrive::~CDRomDrive() = default;

void CDRomDrive::Reset()
{
	m_commandEvent->Cancel();
	m_secondResponseEvent->Cancel();
	m_driveEvent->Cancel();

	m_driveState = DriveState::Idle;

	m_index = 0;
	m_interruptEnable = 0;
	m_interruptFlags = 0;
	m_queuedInterrupt = 0;

	m_pendingCommand = std::nullopt;
	m_secondResponseCommand = std::nullopt;

	m_status.value = 0;
	m_mode.value = 0;

	m_xaFile = 0;
	m_xaChannel = 0;

	m_track = 0;
	m_trackIndex = 0;
	m_trackLocation = {};
	m_seekLocation = {};

	m_firstTrack = 0;
	m_lastTrack = 0;

	m_muteADPCM = true;

	m_parameterBuffer.Reset();
	m_responseBuffer.Reset();
	m_secondResponseBuffer.Reset();
	m_dataBuffer.Reset();

	for ( auto& sector : m_sectorBuffers )
	{
		sector.bytes.fill( 0 );
		sector.size = 0;
	}

	m_readSectorBuffer = 0;
	m_writeSectorBuffer = 0;

	m_pendingSeek = false;
	m_pendingRead = false;
}

uint8_t CDRomDrive::Read( uint32_t registerIndex ) noexcept
{
	dbExpects( m_index < 4 );

	switch ( registerIndex )
	{
		case 0:
		{
			// TODO: XA_ADPCM fifo empty
			const uint8_t status = m_index |
				( (uint8_t)m_parameterBuffer.Empty() << 3 ) |
				( (uint8_t)!m_parameterBuffer.Full() << 4 ) |
				( (uint8_t)!m_responseBuffer.Empty() << 5 ) |
				( (uint8_t)!m_dataBuffer.Empty() << 6 ) |
				( (uint8_t)CommandTransferBusy() << 7 );

			dbLog( "CDRomDrive::Read() -- status [%X]", status );
			return status;
		}

		case 1: // response FIFO (all indices)
		{
			if ( !m_responseBuffer.Empty() )
			{
				const uint8_t value = m_responseBuffer.Pop();
				dbLog( "CDRomDrive::Read() -- response FIFO [%X]", value );
				return value;
			}
			else
			{
				dbLog( "CDRomDrive::Read() -- response FIFO is empty" );
				return 0;
			}
		}

		case 2: // data FIFO (all indices) 8 or 16 bit
			dbBreakMessage( "CDRomDrive::Read() -- use ReadDataFifo() to read variable amount of data" );
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
					m_interruptFlags = m_interruptFlags & ~value;

					if ( value & InterruptFlag::ResetParameterFifo )
						m_parameterBuffer.Clear();

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

void CDRomDrive::SetCDRom( std::unique_ptr<CDRom> cdrom )
{
	m_cdrom = std::move( cdrom );

	if ( m_cdrom )
		StartMotor();
	else
		StopMotor();
}

void CDRomDrive::SendCommand( Command command ) noexcept
{
	if ( CommandTransferBusy() )
	{
		dbLogWarning( "CDRomDrive::SendCommand() -- Command transfer is busy. Canceling first response" );
		m_commandEvent->Cancel();
	}

	if ( m_secondResponseCommand.has_value() )
	{
		dbLogWarning( "CDRomDrive::SendCommand() -- Canceling second response [%X]", *m_secondResponseCommand );
		m_secondResponseCommand = std::nullopt;
		m_secondResponseEvent->Cancel();
	}

	m_pendingCommand = command;
	CheckPendingCommand();
}

void CDRomDrive::QueueSecondResponse( Command command, cycles_t cycles = 19000 ) noexcept // default ticks value is placeholder
{
	dbExpects( !m_secondResponseCommand.has_value() );
	m_secondResponseCommand = command;
	m_secondResponseEvent->Schedule( cycles );
}

void CDRomDrive::ScheduleDriveEvent( DriveState driveState, cycles_t cycles ) noexcept
{
	if ( m_driveState != DriveState::Idle )
	{
		dbLogWarning( "CDRomDrive::ScheduleDriveEvent - Overriding current drive state [%u] with state [%u]", m_driveState, driveState );
		m_driveEvent->Cancel();
	}

	m_driveState = driveState;
	m_driveEvent->Schedule( cycles );
}

void CDRomDrive::CheckPendingCommand() noexcept
{
	// latest command doesn't send until the interrupt are cleared
	if ( m_pendingCommand.has_value() && m_interruptFlags == 0 )
		m_commandEvent->Schedule( GetFirstResponseCycles( *m_pendingCommand ) );
}

void CDRomDrive::CheckInterrupt() noexcept
{
	if ( ( m_interruptFlags & m_interruptEnable ) != 0 )
		m_interruptControl.SetInterrupt( Interrupt::CDRom );
}

void CDRomDrive::ShiftQueuedInterrupt() noexcept
{
	dbExpects( m_interruptFlags == 0 );

	// update read sector on queued interrupt shift for data response
	if ( m_queuedInterrupt == InterruptResponse::ReceivedData )
		m_readSectorBuffer = m_writeSectorBuffer;

	m_interruptFlags = std::exchange( m_queuedInterrupt, uint8_t( 0 ) );
	m_responseBuffer = m_secondResponseBuffer;
	m_secondResponseBuffer.Clear();

	CheckInterrupt();
}

void CDRomDrive::StartMotor() noexcept
{
	if ( m_driveState != DriveState::StartingMotor && !m_status.motorOn )
	{
		ScheduleDriveEvent( DriveState::StartingMotor, CpuCyclesPerSecond );
	}
}

void CDRomDrive::StopMotor() noexcept
{
	m_status.read = false;
	m_status.seek = false;
	m_status.play = false;
	m_status.motorOn = false;

	m_driveState = DriveState::Idle;
	m_driveEvent->Cancel();

	if ( m_cdrom )
		m_cdrom->Seek( 0 ); // seek to beginning of track 1
}

void CDRomDrive::BeginSeeking() noexcept
{
	uint32_t seekCycles = GetSeekCycles();

	if ( m_driveState == DriveState::Seeking )
	{
		dbLogWarning( "CDRomDrive::BeginSeeking -- drive state is already seeking" );
		// TODO: update seek position and recalculate new seek cycles
		seekCycles = m_driveEvent->GetRemainingCycles();
	}

	if ( !m_pendingSeek )
		dbLogWarning( "CDRomDrive::BeginSeeking -- no seek location set" );

	m_pendingSeek = false;

	m_status.read = false;
	m_status.play = false;
	m_status.seek = true;

	ScheduleDriveEvent( DriveState::Seeking, GetSeekCycles() );

	dbAssert( m_cdrom );
	m_cdrom->Seek( m_seekLocation.GetLogicalSector() );
}

void CDRomDrive::BeginReading() noexcept
{
	ClearSectorBuffers();
	m_pendingPlay = false;

	if ( m_pendingSeek )
	{
		m_pendingRead = true;
		BeginSeeking();
		return;
	}

	if ( IsSeeking() )
	{
		m_pendingRead = true;
		return;
	}

	m_pendingRead = false;

	m_status.seek = false;
	m_status.play = false;
	m_status.read = true;

	m_readSectorBuffer = 0;
	m_writeSectorBuffer = 0;

	ScheduleDriveEvent( DriveState::Reading, GetReadCycles() );
}

void CDRomDrive::BeginPlaying( uint8_t track ) noexcept
{
	m_pendingRead = false;

	if ( track == 0 )
	{
		// play from setloc position or current position
	}
	else
	{
		// play chosen Track
		m_pendingSeek = true;
	}

	if ( m_pendingSeek )
	{
		m_pendingPlay = true;
		BeginSeeking();
		return;
	}

	// start playing from current disk position

	m_pendingPlay = false;

	m_status.seek = false;
	m_status.play = true;
	m_status.read = false;

	ClearSectorBuffers();
	m_readSectorBuffer = 0;
	m_writeSectorBuffer = 0;

	ScheduleDriveEvent( DriveState::Playing, GetReadCycles() );
}

cycles_t CDRomDrive::GetFirstResponseCycles( Command command ) const noexcept
{
	/*
	return ( command == Command::Init )
		? 120000
		: ( CanReadDisk() ? 25000 : 15000 );
		*/

	switch ( command )
	{
		case Command::Init:
		case Command::ReadN:	return 75000;

		default:				return 50000;
	}
}

#define COMMAND_CASE( command ) case Command::command:	command();	break

void CDRomDrive::ExecuteCommand() noexcept
{
	dbAssert( m_pendingCommand );
	const Command command = *m_pendingCommand;
	m_pendingCommand = std::nullopt;

	dbLog( "CDRomDrive::ExecuteCommand() -- [%X]", command );

	m_responseBuffer.Clear();

	if ( m_parameterBuffer.Size() < ExpectedCommandParameters[ static_cast<size_t>( command ) ] )
	{
		dbLogWarning( "CDRomDrive::ExecuteCommand() -- Wrong number of parameters" );
		SendError( ErrorCode::WrongNumberOfParameters );
		m_parameterBuffer.Clear();
		CheckInterrupt();
		return;
	}

	switch ( command )
	{
		// Control commands

		case Command::SetFilter:
		{
			// Automatic ADPCM (CD-ROM XA) filter ignores sectors except those which have the same channel and file numbers in their subheader.
			// This is the mechanism used to select which of multiple songs in a single .XA file to play.
			m_xaFile = m_parameterBuffer.Pop();
			m_xaChannel = m_parameterBuffer.Pop();
			SendResponse();
			break;
		}

		case Command::SetMode:
		{
			// The "Ignore Bit" does reportedly force a sector size of 2328 bytes (918h), however, that doesn't seem to be true. Instead, Bit4 seems to cause the controller to ignore the sector size in Bit5
			// (instead, the size is kept from the most recent Setmode command which didn't have Bit4 set). Also, Bit4 seems to cause the controller to ignore the <exact> Setloc position
			// (instead, data is randomly returned from the "Setloc position minus 0..3 sectors"). And, Bit4 causes INT1 to return status.Bit3=set (IdError). Purpose of Bit4 is unknown?
			dbLog( "CDRomDrive::ExecuteCommand -- SetMode" );
			m_mode.value = m_parameterBuffer.Pop();

			// TODO: handle speed change

			SendResponse();

			break;
		}

		case Command::Init:
		{
			dbLog( "CDRomDrive::ExecuteCommand -- Init" );
			SendResponse();

			m_mode.value = 0;

			StartMotor();

			// abort commands
			m_secondResponseCommand = std::nullopt;
			m_secondResponseEvent->Cancel();

			// TODO: abort drive event?

			QueueSecondResponse( Command::Init );
			break;
		}

		case Command::Reset:
		{
			// Resets the drive controller, reportedly, same as opening and closing the drive door.
			// The command executes no matter if/how many parameters are used
			// INT3 indicates that the command was started, but there's no INT that would indicate when the command is finished,
			// so, before sending any further commands, a delay of 1/8 seconds (or 400000h clock cycles) must be issued by software.

			dbLog( "CDRomDrive::ExecuteCommnand -- reset" );
			SendResponse();

			m_secondResponseCommand = std::nullopt;
			m_secondResponseEvent->Cancel();

			m_driveState = DriveState::Idle;
			m_driveEvent->Cancel();

			m_status.value = 0;
			m_status.motorOn = ( m_cdrom != nullptr );

			m_mode.value = 0;
			m_mode.sectorSize = true;

			m_pendingRead = false;
			m_pendingSeek = false;

			m_parameterBuffer.Clear();
			m_responseBuffer.Clear();
			m_secondResponseBuffer.Clear();
			m_dataBuffer.Clear();

			m_readSectorBuffer = 0;
			m_writeSectorBuffer = 0;

			for ( auto& sector : m_sectorBuffers )
			{
				sector.bytes.fill( 0 );
				sector.size = 0;
			}

			if ( m_cdrom )
				m_cdrom->Seek( 0 );

			QueueSecondResponse( Command::Reset, 400000 );

			// TODO: read TOC or change motor speed

			break;
		}

		case Command::MotorOn:
		{
			dbLog( "CDRomDrive::EecuteCommand -- motor on" );
			if ( m_status.motorOn )
			{
				SendError( ErrorCode::WrongNumberOfParameters );
			}
			else
			{
				SendResponse();

				if ( CanReadDisk() )
					StartMotor();

				QueueSecondResponse( Command::MotorOn );
			}
			break;
		}

		case Command::Stop:
		{
			// Stops motor with magnetic brakes (stops within a second or so) (unlike power-off where it'd keep spinning for about 10 seconds),
			// and moves the drive head to the begin of the first track
			dbLog( "CDRomDrive::ExecuteCommand -- stop" );
			const uint32_t stopCycles = m_status.motorOn ? ( m_mode.doubleSpeed ? 25000000 : 13000000 ) : 7000;
			StopMotor();
			SendResponse();
			QueueSecondResponse( Command::Stop, stopCycles );
			break;
		}

		case Command::Pause:
		{
			// Aborts Reading and Playing, the motor is kept spinning, and the drive head maintains the current location within reasonable error
			dbLog( "CDRomDrive::ExecuteCommand -- pause" );

			SendResponse();

			m_driveState = DriveState::Idle;
			m_driveEvent->Cancel();

			m_status.read = false;
			m_status.play = false;
			m_status.seek = false;
			
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
				SendResponse();
				m_seekLocation = CDRom::Location::FromBCD( mm, ss, sect );
				m_pendingSeek = true;
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
			const bool logical = ( command == Command::SeekL );
			dbLog( "CDRomDrive::ExecuteCommand -- %s", logical ? "SeekL" : "SeekP" );

			if ( IsSeeking() )
			{
				dbLogWarning( "CDRomDrive::Execute -- already seeking" );
				// TODO: update position while seeking
			}

			if ( CanReadDisk() )
			{
				SendResponse();
				BeginSeeking();
			}
			else
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			break;
		}

		case Command::SetSession:
		{
			dbLog( "CDRomDrive::ExecuteCommand -- SetSession" );
			if ( !CanReadDisk() || m_driveState == DriveState::Reading || m_driveState == DriveState::Playing )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				const uint8_t session = m_parameterBuffer.Pop();
				if ( session != 0 )
				{
					// TODO: remember session parameter
					SendResponse();
					ScheduleDriveEvent( DriveState::ChangingSession, CpuCyclesPerSecond / 2 );
				}
				else
				{
					SendError( ErrorCode::InvalidArgument );
				}
			}
			break;
		}

		// Read Commands

		case Command::ReadN:
			// Read with retry. The command responds once with "stat,INT3", and then it's repeatedly sending "stat,INT1 --> datablock",
			// that is continued even after a successful read has occured; use the Pause command to terminate the repeated INT1 responses.

		case Command::ReadS:
			// Read without automatic retry. Not sure what that means... does WHAT on errors?
		{
			dbLog( "CDRomDrive::ExecuteCommand -- %s", ( command == Command::ReadN ) ? "ReadN" : "ReadS" );
			if ( !CanReadDisk() )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				SendResponse();
				if ( !m_pendingSeek && ( m_driveState == DriveState::Reading || ( IsSeeking() && m_pendingRead ) ) )
				{
					dbLog( "CDRomDRive::ExecuteCommand -- already reading" );
				}
				else
				{
					// TODO: update position while seeking
					BeginReading();
				}
			}
			break;
		}

		case Command::ReadTOC:
		{
			// Reread the Table of Contents of current session without reset. The command is rather slow, the second response appears after about 1 second delay.
			// The command itself returns only status information (to get the actual TOC info, use GetTD and GetTN commands).
			// Note: The TOC contains information about the tracks on the disk( not file names or so, that kind of information is obtained via Read commands ).
			// The TOC is read automatically on power - up, when opening / closing the drive door, and when changing sessions ( so, normally, it isn't required to use this command).
			SendResponse();
			dbBreak(); // TODO
			break;
		}

		// Status commands

		case Command::GetStat:
		{
			// return status response
			SendResponse();

			// clear shell bit after sending status
			m_status.shellOpen = false;

			break;
		}

		case Command::GetParam:
		{
			dbLog( "CDRomDrive::GetParam" );
			SendResponse();
			m_responseBuffer.Push( m_mode.value );
			m_responseBuffer.Push( 0 ); // always zero
			m_responseBuffer.Push( m_xaFile );
			m_responseBuffer.Push( m_xaChannel );
			break;
		}

		case Command::GetLocL:
		{
			// Retrieves 4 - byte sector header, plus 4 - byte subheader of the current sector.GetlocL can be send during active Read commands
			// ( but, mind that the GetlocL - INT3 - response can't be received until any pending Read-INT1's are acknowledged ).
			// The PSX hardware can buffer a handful of sectors, the INT1 handler receives the <oldest> buffered sector, the GetlocL command
			// returns the header and subheader of the <newest> buffered sector.Note: If the returned <newest> sector number is much bigger than
			// the expected <oldest> sector number, then it's likely that a buffer overrun has occured.
			// GetlocL fails( with error code 80h ) when playing Audio CDs( or Audio Tracks on Data CDs ).These errors occur because Audio sectors
			// don't have any header/subheader (instead, equivalent data is stored in Subchannel Q, which can be read with GetlocP).
			// GetlocL also fails( with error code 80h ) when the drive is in Seek phase( such like shortly after a new ReadN / ReadS command ).
			// In that case one can retry issuing GetlocL( until it passes okay, ie.until the seek has completed ).During Seek, the drive seems to
			// decode only Subchannel position data( but no header / subheader data ), accordingly GetlocL won't work during seek (however, GetlocP does work during Seek).
			dbLog( "CDRomDrive::GetLocL" );

			if ( !CanReadDisk() || IsSeeking() )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				// return 4 byte sector header
				m_responseBuffer.Push( BinaryToBCD( m_seekLocation.minute ) );
				m_responseBuffer.Push( BinaryToBCD( m_seekLocation.second ) );
				m_responseBuffer.Push( BinaryToBCD( m_seekLocation.sector ) );
				m_responseBuffer.Push( 0 ); // mode?

				// return 4 byte subheader of the current sector
				m_responseBuffer.Push( m_xaFile );
				m_responseBuffer.Push( m_xaChannel );
				m_responseBuffer.Push( 0 ); // sm?
				m_responseBuffer.Push( 0 ); // ci?

				m_interruptFlags = InterruptResponse::First;
			}
			break;
		}

		case Command::GetLocP:
		{
			// Retrieves 8 bytes of position information from Subchannel Q with ADR=1.
			// Mainly intended for displaying the current audio position during Play. All results are in BCD.
			dbLog( "CDRomDrive::GetLocP" );

			if ( !CanReadDisk() )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				// TODO: update position while seeking

				m_responseBuffer.Push( m_track );
				m_responseBuffer.Push( m_trackIndex );
				m_responseBuffer.Push( BinaryToBCD( m_trackLocation.minute ) );
				m_responseBuffer.Push( BinaryToBCD( m_trackLocation.second ) );
				m_responseBuffer.Push( BinaryToBCD( m_trackLocation.sector ) );
				m_responseBuffer.Push( BinaryToBCD( m_seekLocation.minute ) );
				m_responseBuffer.Push( BinaryToBCD( m_seekLocation.second ) );
				m_responseBuffer.Push( BinaryToBCD( m_seekLocation.sector ) );
				m_interruptFlags = InterruptResponse::First;
			}
			break;
		}

		case Command::GetTrackNumber:
		{
			// Get first track number, and last track number in the TOC of the current Session.
			// The number of tracks in the current session can be calculated as (last-first+1).
			// The first track number is usually 01h in the first (or only) session,
			// and "last track of previous session plus 1" in further sessions.
			dbLog( "CDRomDrive::ExecuteCommand -- GetTrackNumber" );

			if ( CanReadDisk() )
			{
				SendResponse();
				m_responseBuffer.Push( m_firstTrack );
				m_responseBuffer.Push( m_lastTrack );
			}
			else
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			break;
		}

		case Command::GetTD:
		{
			// For a disk with NN tracks, parameter values 01h..NNh return the start of the specified track,
			// parameter value 00h returns the end of the last track, and parameter values bigger than NNh return error code 10h.
			// The GetTD values are relative to Index = 1 and are rounded down to second boundaries
			if ( !CanReadDisk() )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				const auto track = m_parameterBuffer.Pop();
				dbLog( "CDRomDrive::ExecuteCommand -- GetTD [%X]", (uint32_t)track );

				// TODO: send error if track count is out of range

				// TODO: get location of track from cue sheet

				SendResponse();

				// send start of disk position for now
				m_responseBuffer.Push( 0 ); // mm
				m_responseBuffer.Push( 2 ); // ss
			}
			break;
		}

		case Command::GetQ:
		{
			SendResponse();
			dbBreak(); // TODO
			break;
		}

		case Command::GetID:
		{
			dbLog( "CDRomDrive::ExecuteCommand -- GetID" );
			if ( CanReadDisk() )
			{
				SendResponse();
				QueueSecondResponse( Command::GetID, 0x0004a00 ); // TODO: add ticks when waiting for motor
			}
			else
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			break;
		}

		// CD audio commands

		case Command::Mute:
		{
			// Turn off audio streaming to SPU (affects both CD-DA and XA-ADPCM).
			// Even when muted, the CDROM controller is internally processing audio sectors( as seen in 1F801800h.Bit2, which works as usually for XA - ADPCM ),
			// muting is just forcing the CD output volume to zero.
			// Mute is used by Dino Crisis 1 to mute noise during modchip detection.
			dbLog( "CDRomDrive::ExecuteCommand -- Mute" );
			// TODO
			SendResponse();
			break;
		}

		case Command::Demute:
		{
			// Turn on audio streaming to SPU (affects both CD-DA and XA-ADPCM). The Demute command is needed only if one has formerly used the Mute command
			// (by default, the PSX is demuted after power-up (...and/or after Init command?), and is demuted after cdrom-booting).
			dbLog( "CDRomDrive::ExecuteCommand -- Demute" );
			// TODO
			SendResponse();
			break;
		}

		case Command::Play:
		{
			// Starts CD Audio Playback. The parameter is optional, if there's no parameter given (or if it is 00h), then play either starts at Setloc position
			// (if there was a pending unprocessed Setloc), or otherwise starts at the current location
			// (eg. the last point seeked, or the current location of the current song; if it was already playing).
			// For a disk with N songs, Parameters 1..N are starting the selected track. Parameters N+1..99h are restarting the begin of current track.
			// The motor is switched off automatically when Play reaches the end of the disk, and INT4(stat) is generated (with stat.bit7 cleared).
			// The track parameter seems to be ignored when sending Play shortly after power - up( ie.when the drive hasn't yet read the TOC).
			dbLog( "CDRomDrive::ExecuteCommand -- Play" );
			uint8_t track = 0;
			if ( !m_parameterBuffer.Empty() )
				track = m_parameterBuffer.Pop();

			SendResponse();
			BeginPlaying( track );
			break;
		}

		case Command::Forward:
		{
			dbLog( "CDRomDrive::ExecuteCommand -- Forward" );
			if ( m_driveState != DriveState::Playing )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				SendResponse();
				// TODO: skip sectors forward
			}
			break;
		}

		case Command::Backward:
		{
			dbLog( "CDRomDrive::ExecuteCommand -- Backward" );
			if ( m_driveState != DriveState::Playing )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				SendResponse();
				// TODO: skip sectors backward
			}
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

				default:
					dbBreak(); // TODO
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
			dbLogWarning( "CDRomDrive::ExecuteCommand() -- Invalid command" );
			SendError( ErrorCode::InvalidCommand );
			break;
		}
	}

	dbAssertMessage( m_interruptFlags != 0, "No interrupt for command [%X]", command ); // there should be a response
	m_parameterBuffer.Clear();
	CheckInterrupt();
}

#undef COMMAND_CASE

void CDRomDrive::ExecuteSecondResponse() noexcept
{
	dbAssert( m_secondResponseCommand.has_value() );
	const Command command = *m_secondResponseCommand;
	m_secondResponseCommand = std::nullopt;

	dbLog( "CDRomDrive::ExecuteSecondResponse() -- [%X]", command );

	dbAssert( m_queuedInterrupt == 0 ); // cannot queue more than 1 interrupt
	m_secondResponseBuffer.Clear();

	switch ( command )
	{
		case Command::GetID:
		{
			dbLog( "CDRomDrive::ExecuteSecondResponse -- GetID" );
			m_status.read = false;
			m_status.seek = false;
			m_status.play = false;
			m_status.motorOn = ( m_cdrom != nullptr );

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
		case Command::MotorOn:
		case Command::Stop:
		case Command::Pause:
			SendSecondResponse();
			break;

		case Command::Reset:
			// reset command does not generate an INT when the command finishes
			break;

		default:
			dbBreakMessage( "Command %X does not have a second response", command );
			break;
	}

	dbAssert( m_queuedInterrupt != 0 ); // there should be a second response
	if ( m_interruptFlags == 0 )
		ShiftQueuedInterrupt();
}

void CDRomDrive::ExecuteDrive() noexcept
{
	const DriveState state = std::exchange( m_driveState, DriveState::Idle );
	switch ( state )
	{
		case DriveState::Idle:
			dbBreak(); // this should not happen
			break;

		case DriveState::StartingMotor:
		{
			dbLog( " CDRomDrive::ExecuteDrive -- motor on complete" );
			m_status.read = false;
			m_status.seek = false;
			m_status.play = false;
			m_status.motorOn = true;
			break;
		}

		case DriveState::Seeking:
		{
			dbLog( " CDRomDrive::ExecuteDrive -- seek complete" );

			// TODO: check if seek was successful

			m_status.seek = false;

			if ( m_pendingRead )
			{
				BeginReading();
			}
			else
			{
				// response only sent if there is no pending play or read
				SendSecondResponse(); // TODO: not sure if the response should be sent here or in the second response event
			}
			break;
		}

		case DriveState::Reading:
		case DriveState::ReadingNoRetry:
		case DriveState::Playing:
		{
			dbLog( "CDRomDrive::ExecuteDrive -- read complete" );

			m_status.read = false;

			ScheduleDriveEvent( state, GetReadCycles() );

			CDRom::Sector sector;
			if ( !m_cdrom->ReadSector( sector ) )
			{
				dbLogWarning( "CDRomDrive::ExecuteDrive -- Reading from end of disk" );
				return;
			}

			if ( state == DriveState::Playing )
			{
				// play CD-DA audio
				dbLogWarning( "Skipping CD-DA sector" );
				return;
			}

			if ( ( sector.header.mode == 2 ) &&
				m_mode.xaadpcm &&
				sector.mode2.subHeader.subMode.audio &&
				sector.mode2.subHeader.subMode.realTime )
			{
				// read XA-ADPCM
				dbLogWarning( "Skipping XA-DCPM sector" );
				return;
			}

			m_writeSectorBuffer = ( m_writeSectorBuffer + 1 ) % NumSectorBuffers;
			auto& buffer = m_sectorBuffers[ m_writeSectorBuffer ];

			if ( buffer.size > 0 )
				dbLog( "CDRomDrive::ExecuteDrive -- overwriting buffer [%u]", m_writeSectorBuffer );

			if ( m_mode.sectorSize )
			{
				std::copy_n( sector.audio.data() + CDRom::SyncSize, DataBufferSize, buffer.bytes.data() );
				buffer.size = DataBufferSize;
			}
			else
			{
				auto readSectorData = [&buffer]( const uint8_t* src )
				{
					std::copy_n( src, CDRom::DataBytesPerSector, buffer.bytes.data() );
				};

				switch ( sector.header.mode )
				{
					case 0:
						std::fill_n( buffer.bytes.data(), CDRom::DataBytesPerSector, uint8_t( 0 ) );
						break;

					case 1:		readSectorData( sector.mode1.data.data() );			break;
					case 2:		readSectorData( sector.mode2.form1.data.data() );	break;
					case 3:		readSectorData( sector.mode2.form2.data.data() );	break;

					default:
						dbBreak();
				}
				buffer.size = CDRom::DataBytesPerSector;
			}

			if ( m_queuedInterrupt == 0 )
			{
				SendSecondResponse( InterruptResponse::ReceivedData );
			}
			else if ( state == DriveState::Reading )
			{
				dbLogWarning( "CDRomDrive::ExecuteDrive -- delaying data response" );
				// TODO: try interrupt again later
			}
			break;
		}

		case DriveState::ChangingSession:
			dbBreak(); // TODO
			break;
	}

	if ( m_interruptFlags == 0 && m_queuedInterrupt != 0 )
		ShiftQueuedInterrupt();
}

void CDRomDrive::LoadDataFifo() noexcept
{
	dbLog( "CDRomDrive::LoadDataFifo()" );

	if ( !m_dataBuffer.Empty() )
	{
		dbLogWarning( "CDRomDrive::LoadDataFifo -- data buffer is not empty [%u]", m_dataBuffer.Size() );
		return;
	}

	auto& sector = m_sectorBuffers[ m_readSectorBuffer ];

	if ( sector.size > 0 )
	{
		dbLog( "CDRomDrive::LoadDataFifo -- loaded %u bytes from buffer %u", sector.size, m_readSectorBuffer );
		m_dataBuffer.Push( sector.bytes.data(), sector.size );
		sector.size = 0;
	}
	else
	{
		dbLogWarning( "CDRomDrive::LoadDataFifo -- reading from empty sector buffer" );
		m_dataBuffer.Push( sector.bytes.data(), DataBufferSize );
	}

	// the PSX skips all unprocessed sectors and jumps straight to the newest sector

	auto& nextSector = m_sectorBuffers[ m_writeSectorBuffer ];
	if ( nextSector.size > 0 )
	{
		dbLog( "sending additional interrupt for missed sector" );
		SendSecondResponse( InterruptResponse::ReceivedData );
		if ( m_interruptFlags == 0 )
			ShiftQueuedInterrupt();
	}
}

// CDROM control commands

}