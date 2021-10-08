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

CDRomDrive::CDRomDrive( InterruptControl& interruptControl, EventManager& eventManager )
	: m_interruptControl{ interruptControl }
{
	m_commandEvent = eventManager.CreateEvent( "CDRomDrive command event", [this]( auto )
		{
			ExecuteCommand();
		} );

	m_secondResponseEvent = eventManager.CreateEvent( "CDRomDrive second response", [this]( auto )
		{
			ExecuteSecondResponse();
		} );

	m_driveEvent = eventManager.CreateEvent( "CDRomDrive drive event", [this]( auto )
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

	m_pendingCommand = Command::Invalid;
	m_secondResponseCommand = Command::Invalid;

	m_status.value = 0;
	m_mode.value = 0;

	m_file = 0;
	m_channel = 0;

	m_track = 0;
	m_trackIndex = 0;
	m_trackLocation = {};
	m_seekLocation = {};

	m_currentSector = 0;

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

	if ( m_secondResponseCommand != Command::Invalid )
	{
		dbLogWarning( "CDRomDrive::SendCommand() -- Canceling second response [%X]", m_secondResponseCommand );
		m_secondResponseCommand = Command::Invalid;
		m_secondResponseEvent->Cancel();
	}

	m_pendingCommand = command;
	CheckPendingCommand();
}

void CDRomDrive::QueueSecondResponse( Command command, cycles_t cycles = 0x0004a00 ) noexcept // default ticks value is placeholder
{
	dbExpects( m_secondResponseCommand == Command::Invalid );
	m_secondResponseCommand = command;
	m_secondResponseEvent->Schedule( cycles );
}

void CDRomDrive::ScheduleDriveEvent( DriveState driveState, cycles_t cycles ) noexcept
{
	dbExpects( m_driveState == DriveState::Idle );
	m_driveState = driveState;
	m_driveEvent->Schedule( cycles );
}

void CDRomDrive::CheckPendingCommand() noexcept
{
	// latest command doesn't send until the interrupt are cleared
	if ( m_pendingCommand != Command::Invalid && m_interruptFlags == 0 )
		m_commandEvent->Schedule( GetFirstResponseCycles( m_pendingCommand ) );
}

void CDRomDrive::CheckInterrupt() noexcept
{
	if ( ( m_interruptFlags & m_interruptEnable ) != 0 )
		m_interruptControl.SetInterrupt( Interrupt::CDRom );
}

void CDRomDrive::ShiftQueuedInterrupt() noexcept
{
	dbExpects( m_interruptFlags == 0 );
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

#define COMMAND_CASE( command ) case Command::command:	command();	break

void CDRomDrive::ExecuteCommand() noexcept
{
	const Command command = std::exchange( m_pendingCommand, Command::Invalid );
	dbLog( "CDRomDrive::ExecuteCommand() -- [%X]", command );

	m_responseBuffer.Clear();

	switch ( command )
	{
		// Control commands

		case Command::SetFilter:
		{
			// Automatic ADPCM (CD-ROM XA) filter ignores sectors except those which have the same channel and file numbers in their subheader.
			// This is the mechanism used to select which of multiple songs in a single .XA file to play.
			dbBreak(); // TODO
			break;
		}

		case Command::SetMode:
		{
			// The "Ignore Bit" does reportedly force a sector size of 2328 bytes (918h), however, that doesn't seem to be true. Instead, Bit4 seems to cause the controller to ignore the sector size in Bit5
			// (instead, the size is kept from the most recent Setmode command which didn't have Bit4 set). Also, Bit4 seems to cause the controller to ignore the <exact> Setloc position
			// (instead, data is randomly returned from the "Setloc position minus 0..3 sectors"). And, Bit4 causes INT1 to return status.Bit3=set (IdError). Purpose of Bit4 is unknown?
			dbLog( "CDRomDrive::ExecuteCommand -- SetMode" );
			if ( m_parameterBuffer.Empty() )
			{
				SendError( ErrorCode::WrongNumberOfParameters );
			}
			else
			{
				m_mode.value = m_parameterBuffer.Pop();

				// TODO: handle speed change

				SendResponse();
			}

			break;
		}

		case Command::Init:
		{
			dbLog( "CDRomDrive::ExecuteCommand -- Init" );

			m_mode.value = 0;

			StartMotor();

			// abort commands
			m_pendingCommand = Command::Invalid;
			m_commandEvent->Cancel();
			m_secondResponseCommand = Command::Invalid;
			m_secondResponseEvent->Cancel();

			SendResponse();
			QueueSecondResponse( Command::Init );
			break;
		}

		case Command::Reset:
		{
			// Resets the drive controller, reportedly, same as opening and closing the drive door.
			// The command executes no matter if/how many parameters are used
			dbLog( "CDRomDrive::ExecuteCommnand -- reset" );

			m_secondResponseCommand = Command::Invalid;
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

			m_currentSector = 0;
			m_readSectorBuffer = 0;
			m_writeSectorBuffer = 0;

			for ( auto& sector : m_sectorBuffers )
			{
				sector.bytes.fill( 0 );
				sector.size = 0;
			}

			if ( m_cdrom )
				m_cdrom->Seek( 0 );

			// TODO: read TOC or change motor speed

			SendResponse();
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
				if ( m_secondResponseCommand != Command::MotorOn )
				{
					if ( CanReadDisk() )
						StartMotor();

					QueueSecondResponse( Command::MotorOn );
				}

				SendResponse();
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

			m_driveState = DriveState::Idle;
			m_driveEvent->Cancel();

			m_status.read = false;
			m_status.play = false;
			m_status.seek = false;
			
			SendResponse();
			QueueSecondResponse( Command::Pause );
			break;
		}

		// Seek commands

		case Command::SetLoc:
		{
			if ( m_parameterBuffer.Size() < 3 )
			{
				SendError( ErrorCode::WrongNumberOfParameters );
			}
			else
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
					m_pendingSeek = true;
					SendResponse();
				}
				else
				{
					SendError( ErrorCode::InvalidArgument );
				}
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
				BeginSeeking();
				SendResponse();
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
			if ( m_parameterBuffer.Empty() )
			{
				SendError( ErrorCode::WrongNumberOfParameters );
			}
			else if ( !CanReadDisk() || m_driveState == DriveState::Reading || m_driveState == DriveState::Playing )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				const uint8_t session = m_parameterBuffer.Pop();
				if ( session != 0 )
				{
					// TODO: remember session parameter
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
				if ( !m_pendingSeek && ( m_driveState == DriveState::Reading || ( IsSeeking() && m_pendingRead ) ) )
				{
					dbLog( "CDRomDRive::ExecuteCommand -- already reading" );
				}
				else
				{
					// TODO: update position while seeking
					BeginReading();
					SendResponse();
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
			dbBreak();
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
			m_responseBuffer.Push( m_file );
			m_responseBuffer.Push( m_channel );
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
				m_responseBuffer.Push( m_file );
				m_responseBuffer.Push( m_channel );
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
			if ( m_parameterBuffer.Empty() )
			{
				SendError( ErrorCode::WrongNumberOfParameters );
			}
			else if ( !CanReadDisk() )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				const auto track = m_parameterBuffer.Pop();
				dbLog( "CDRomDrive::ExecuteCommand -- GetTD [%X]", (uint32_t)track );

				// TODO: send error if track count is out of range

				dbBreak(); // TODO: get location of track from cue sheet

				SendResponse();
				m_responseBuffer.Push( 0 ); // mm
				m_responseBuffer.Push( 0 ); // ss
			}
			break;
		}

		case Command::GetQ:
		{
			dbBreak(); // TODO
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
			dbBreak(); // TODO
			break;
		}

		case Command::Demute:
		{
			// Turn on audio streaming to SPU (affects both CD-DA and XA-ADPCM). The Demute command is needed only if one has formerly used the Mute command
			// (by default, the PSX is demuted after power-up (...and/or after Init command?), and is demuted after cdrom-booting).
			dbLog( "CDRomDrive::ExecuteCommand -- Demute" );
			dbBreak(); // TODO
			break;
		}

		case Command::Play:
		{
			dbLog( "CDRomDrive::ExecuteCommand -- Play" );
			dbBreak(); // TODO
			break;
		}

		case Command::Forward:
		{
			dbLog( "CDRomDrive::ExecuteCommand -- Forward" );
			dbBreak(); // TODO
			break;
		}

		case Command::Backward:
		{
			dbLog( "CDRomDrive::ExecuteCommand -- Backward" );
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
			dbBreakMessage( "CDRomDrive::ExecuteCommand() -- Invalid command" );
			SendError( ErrorCode::InvalidCommand );
			break;
		}
	}

	dbAssert( m_interruptFlags != InterruptResponse::None ); // there should be a response
	m_parameterBuffer.Clear();
	CheckInterrupt();
}

#undef COMMAND_CASE

void CDRomDrive::ExecuteSecondResponse() noexcept
{
	const auto command = std::exchange( m_secondResponseCommand, Command::Invalid );
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
				SendSecondResponse();
			}
			break;
		}

		case DriveState::Reading:
		case DriveState::ReadingSingle:
		case DriveState::Playing:
		{
			dbLog( "CDRomDrive::ExecuteDrive -- read complete" );

			m_status.read = false;

			if ( !m_cdrom->ReadSync() )
			{
				dbLogWarning( "CDRomDrive::ExecuteDrive -- invalid sync" );
				SendSecondError( ErrorCode::SeekFailed ); // TODO: should this happen at the end of seek?
			}

			auto& sector = m_sectorBuffers[ m_writeSectorBuffer ];

			const bool readFullSector = m_mode.sectorSize;
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

			// schedule next sector to read
			m_currentSector++;
			m_cdrom->Seek( m_currentSector );

			if ( state != DriveState::ReadingSingle )
				ScheduleDriveEvent( state, GetReadCycles() );

			SendSecondResponse( InterruptResponse::ReceivedData );

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
		dbLogWarning( "data buffer is not empty" );
		return;
	}

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