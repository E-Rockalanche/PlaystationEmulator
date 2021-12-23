#include "CDRomDrive.h"

#include "DMA.h"
#include "EventManager.h"
#include "InterruptControl.h"

#include <stdx/scope.h>

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

const std::array<std::array<int16_t, 29>, 7> XaAdpcmZigZagTables
{ {
	{ 0,       0,       0,       0,       0,       -0x0002, +0x000A, -0x0022, +0x0041, -0x0054, +0x0034, +0x0009, -0x010A, +0x0400, -0x0A78, +0x234C, +0x6794, -0x1780, +0x0BCD, -0x0623, +0x0350, -0x016D, +0x006B, +0x000A, -0x0010, +0x0011, -0x0008, +0x0003, -0x0001 },
	{ 0,       0,       0,       -0x0002, 0,       +0x0003, -0x0013, +0x003C, -0x004B, +0x00A2, -0x00E3, +0x0132, -0x0043, -0x0267, +0x0C9D, +0x74BB, -0x11B4, +0x09B8, -0x05BF, +0x0372, -0x01A8, +0x00A6, -0x001B, +0x0005, +0x0006, -0x0008, +0x0003, -0x0001, 0 },
	{ 0,       0,       -0x0001, +0x0003, -0x0002, -0x0005, +0x001F, -0x004A, +0x00B3, -0x0192, +0x02B1, -0x039E, +0x04F8, -0x05A6, +0x7939, -0x05A6, +0x04F8, -0x039E, +0x02B1, -0x0192, +0x00B3, -0x004A, +0x001F, -0x0005, -0x0002, +0x0003, -0x0001, 0,       0 },
	{ 0,       -0x0001, +0x0003, -0x0008, +0x0006, +0x0005, -0x001B, +0x00A6, -0x01A8, +0x0372, -0x05BF, +0x09B8, -0x11B4, +0x74BB, +0x0C9D, -0x0267, -0x0043, +0x0132, -0x00E3, +0x00A2, -0x004B, +0x003C, -0x0013, +0x0003, 0,       -0x0002, 0,       0,       0 },
	{ -0x0001, +0x0003, -0x0008, +0x0011, -0x0010, +0x000A, +0x006B, -0x016D, +0x0350, -0x0623, +0x0BCD, -0x1780, +0x6794, +0x234C, -0x0A78, +0x0400, -0x010A, +0x0009, +0x0034, -0x0054, +0x0041, -0x0022, +0x000A, -0x0001, 0,       +0x0001, 0,       0,       0 },
	{ +0x0002, -0x0008, +0x0010, -0x0023, +0x002B, +0x001A, -0x00EB, +0x027B, -0x0548, +0x0AFA, -0x16FA, +0x53E0, +0x3C07, -0x1249, +0x080E, -0x0347, +0x015B, -0x0044, -0x0017, +0x0046, -0x0023, +0x0011, -0x0005, 0,       0,       0,       0,       0,       0 },
	{ -0x0005, +0x0011, -0x0023, +0x0046, -0x0017, -0x0044, +0x015B, -0x0347, +0x080E, -0x1249, +0x3C07, +0x53E0, -0x16FA, +0x0AFA, -0x0548, +0x027B, -0x00EB, +0x001A, +0x002B, -0x0023, +0x0010, -0x0008, +0x0002, 0,       0,       0,       0,       0,       0 }
} };

int16_t ZigZagInterpolate( const int16_t* ringBuffer, const int16_t* zigZagTable, uint8_t p )
{
	int32_t sum = 0;
	for ( uint32_t i = 0; i < 29; ++i )
	{
		sum += ringBuffer[ ( p - i - 1 ) & 0x1f ] * zigZagTable[ i ] / 0x8000;
	}
	return static_cast<int16_t>( std::clamp<int32_t>( sum, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max() ) );
}

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
			ExecuteCommandSecondResponse();
		} );

	m_driveEvent = eventManager.CreateEvent( "CDRomDrive drive event", [this]( cycles_t )
		{
			ExecuteDriveState();
		} );

	m_xaAdpcmSampleBuffer = std::make_unique<int16_t[]>( XaAdpcmSampleBufferSize );
}

CDRomDrive::~CDRomDrive() = default;

void CDRomDrive::Reset()
{
	m_commandEvent->Cancel();
	m_secondResponseEvent->Cancel();
	m_driveEvent->Cancel();

	m_driveState = DriveState::Idle;

	m_status.index = 0;
	m_interruptEnable = 0;
	m_interruptFlags = 0;
	m_queuedInterrupt = 0;

	m_volumes = ChannelVolumes{};
	m_nextVolumes = ChannelVolumes{};

	m_pendingCommand = std::nullopt;
	m_secondResponseCommand = std::nullopt;

	m_driveStatus.value = 0;
	m_driveStatus.motorOn = ( m_cdrom != nullptr );
	m_mode.value = 0;

	m_xaFilter = XaFilter{};

	m_track = 0;
	m_trackIndex = 0;
	m_trackLocation = {};
	m_seekLocation = {};

	m_firstTrack = 0;
	m_lastTrack = 0;

	m_muted = false;
	m_muteADPCM = false;

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

	m_currentSectorHeaders.reset();

	m_pendingSeek = false;
	m_pendingRead = false;
	m_pendingPlay = false;

	std::fill_n( m_xaAdpcmSampleBuffer.get(), XaAdpcmSampleBufferSize, int16_t{} );
	m_oldXaAdpcmSamples.fill( 0 );
	m_resampleRingBuffers[ 0 ].fill( 0 );
	m_resampleRingBuffers[ 1 ].fill( 0 );
	m_resampleP = 0;

	m_audioBuffer.Reset();

	UpdateStatus();
}

uint8_t CDRomDrive::Read( uint32_t registerIndex ) noexcept
{
	switch ( registerIndex )
	{
		case 0:
		{
			dbLogDebug( "CDRomDrive::Read() -- status [%X]", m_status.value );
			return m_status.value;
		}

		case 1: // response FIFO (all indices)
		{
			if ( !m_responseBuffer.Empty() )
			{
				const uint8_t value = m_responseBuffer.Pop();
				dbLogDebug( "CDRomDrive::Read() -- response FIFO [%X]", value );
				UpdateStatus();
				return value;
			}
			else
			{
				dbLogWarning( "CDRomDrive::Read() -- response FIFO is empty" );
				return 0;
			}
		}

		case 2: // data FIFO (all indices) 8 or 16 bit
		{
			const uint8_t value = !m_dataBuffer.Empty() ? m_dataBuffer.Pop() : uint8_t( 0xff );
			dbLogDebug( "CDRomDrive::Read() -- data fifo [%X]", value );
			UpdateStatus();
			return value;
		}

		case 3:
			switch ( m_status.index )
			{
				case 0:
				case 2:
					// interrupt enable
					dbLogDebug( "CDRomDrive::Read() -- interrupt enable [%X]", m_interruptEnable );
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
	switch ( registerIndex )
	{
		case 0:
			m_status.index = value % 4;
			break;

		case 1:
			switch ( m_status.index )
			{
				case 0: // command register
					dbLogDebug( "CDRomDrive::Write() -- send command [%X]", value );
					SendCommand( static_cast<Command>( value ) );
					break;

				case 1: // sound map data out
					dbLogWarning( "CDRomDrive::Write() -- ignoring sound map data out [%X]", value );
					break;

				case 2: // sound map coding info
					dbLogWarning( "CDRomDrive::Write() -- ignoring sound map coding info [%X]", value );
					break;

				case 3: // audio volume for right-cd-out to right-spu-input
					dbLogDebug( "CDRomDrive::Write() -- right-cd-out to right-spu-input [%X]", value );
					m_nextVolumes.rightToRight = value;
					break;
			}
			break;

		case 2:
			switch ( m_status.index )
			{
				case 0: // parameter fifo
					dbLogDebug( "CDRomDrive::Write() -- paramater [%X]", value );
					m_parameterBuffer.Push( value );
					UpdateStatus();
					break;

				case 1: // interrupt enable
					dbLogDebug( "CDRomDrive::Write() -- interrupt enable [%X]", value );
					m_interruptEnable = value;
					CheckInterrupt();
					break;

				case 2: // left-cd-out to left-spu-input
					dbLogDebug( "CDRomDrive::Write() -- left-cd-out to left-spu-input [%X]", value );
					m_nextVolumes.leftToLeft = value;
					break;

				case 3: // right-cd-out to left-cd-input
					dbLogDebug( "CDRomDrive::Write() -- right-cd-out to left-cd-input [%X]", value );
					m_nextVolumes.rightToLeft = value;
					break;
			}
			break;

		case 3:
			switch ( m_status.index )
			{
				case 0: // request register
				{
					dbLogDebug( "CDRomDrive::Write() -- data request [%X]", value );

					if ( value & RequestRegister::WantData )
						RequestData();
					else
						m_dataBuffer.Clear();

					UpdateStatus();
					break;
				}

				case 1: // ack interrupt flags
				{
					dbLogDebug( "CDRomDrive::Write() -- interrupt flag [%X]", value );
					m_interruptFlags = m_interruptFlags & ~value; // write 1 to ack/reset

					// m_responseBuffer.Clear();

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

					UpdateStatus();
					break;
				}

				case 2: // audio volume for left-cd-out to right-spu-input
					dbLogDebug( "CDRomDrive::Write() -- left-cd-out to right-spu-input [%X]", value );
					m_nextVolumes.leftToRight = value;
					break;

				case 3: // audio volume apply (write bit5=1)
				{
					dbLogDebug( "CDRomDrive::Write() -- audio volume apply" );
					m_muteADPCM = value & AudioVolumeApply::MuteADPCM;

					// TODO: change audio volume
					if ( value & AudioVolumeApply::ChangeAudioVolume )
						m_volumes = m_nextVolumes;

					break;
				}
			}
			break;
	}
}

void CDRomDrive::SetCDRom( std::unique_ptr<CDRom> cdrom )
{
	dbLogDebug( "CDRomDrive::SetCDRom" );

	if ( m_cdrom )
	{
		StopMotor();
		m_currentSectorHeaders.reset();
		m_pendingCommand.reset();
		m_commandEvent->Cancel();
		m_secondResponseCommand.reset();
		m_secondResponseEvent->Cancel();
		m_queuedInterrupt = 0;

		SendSecondError( ErrorCode::DriveDoorOpened, DriveStatusError::IdError );
	}

	m_cdrom = std::move( cdrom );

	if ( m_cdrom )
		StartMotor();

	if ( m_interruptFlags == 0 )
		ShiftQueuedInterrupt();
}

void CDRomDrive::DmaRead( uint32_t* data, uint32_t count )
{
	const uint32_t available = std::min( count * 4, m_dataBuffer.Size() );
	m_dataBuffer.Pop( reinterpret_cast<uint8_t*>( data ), available );
	const uint32_t over = count * 4 - available;
	std::fill_n( reinterpret_cast<uint8_t*>( data ) + available, over, uint8_t( 0xff ) );
	UpdateStatus();
}

void CDRomDrive::UpdateStatus() noexcept
{
	m_status.adpBusy = false;
	m_status.parameterFifoEmpty = m_parameterBuffer.Empty();
	m_status.parameterFifoNotFull = !m_parameterBuffer.Full();
	m_status.responseFifoNotEmpty = !m_responseBuffer.Empty();
	m_status.dataFifoNotEmpty = !m_dataBuffer.Empty();
	m_status.commandTransferBusy = CommandTransferBusy();

	m_dma->SetRequest( Dma::Channel::CdRom, m_status.dataFifoNotEmpty );
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
	UpdateStatus();
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
	{
		m_interruptControl.SetInterrupt( Interrupt::CDRom );
	}
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
	UpdateStatus();
}

void CDRomDrive::StartMotor() noexcept
{
	if ( m_driveState != DriveState::StartingMotor && !m_driveStatus.motorOn )
	{
		ScheduleDriveEvent( DriveState::StartingMotor, CpuCyclesPerSecond );
	}
}

void CDRomDrive::StopMotor() noexcept
{
	m_driveStatus.read = false;
	m_driveStatus.seek = false;
	m_driveStatus.play = false;
	m_driveStatus.motorOn = false;

	m_driveState = DriveState::Idle;
	m_driveEvent->Cancel();

	if ( m_cdrom )
		m_cdrom->Seek( 0 ); // seek to beginning of track 1
}

void CDRomDrive::BeginSeeking() noexcept
{
	uint32_t seekCycles = GetSeekCycles();

	if ( m_driveState == DriveState::Seeking )
		seekCycles = m_driveEvent->GetRemainingCycles();

	if ( !m_driveStatus.motorOn )
		seekCycles += ( m_driveState == DriveState::StartingMotor ) ? m_driveEvent->GetRemainingCycles() : CpuCyclesPerSecond;

	if ( !m_pendingSeek )
		dbLogWarning( "CDRomDrive::BeginSeeking -- no seek location set" );

	m_pendingSeek = false;

	m_driveStatus.read = false;
	m_driveStatus.play = false;
	m_driveStatus.seek = true;

	m_currentSectorHeaders.reset();

	ScheduleDriveEvent( DriveState::Seeking, seekCycles );

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

	m_driveStatus.seek = false;
	m_driveStatus.play = false;
	m_driveStatus.read = true;

	m_readSectorBuffer = 0;
	m_writeSectorBuffer = 0;

	ScheduleDriveEvent( DriveState::Reading, GetReadCycles() );
}

void CDRomDrive::BeginPlaying( uint8_t track ) noexcept
{
	m_pendingRead = false;

	if ( track == 0 )
	{
		// TODO: play from setloc position or current position. Set current track from location
	}
	else
	{
		// play chosen Track
		m_pendingSeek = true;
		m_track = track;
	}

	if ( m_pendingSeek )
	{
		m_pendingPlay = true;
		BeginSeeking();
		return;
	}

	// start playing from current disk position

	m_pendingPlay = false;

	m_driveStatus.seek = false;
	m_driveStatus.play = true;
	m_driveStatus.read = false;

	ClearSectorBuffers();
	m_readSectorBuffer = 0;
	m_writeSectorBuffer = 0;

	m_currentSectorHeaders.reset();

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

	stdx::scope_exit onExit( [this, command]
		{
			dbAssertMessage( m_interruptFlags != 0, "No interrupt for command [%X]", command ); // there should be a response
			m_parameterBuffer.Clear();
			CheckInterrupt();
			UpdateStatus();
		} );

	dbLogDebug( "CDRomDrive::ExecuteCommand() -- [%X]", command );

	m_commandEvent->Cancel();
	m_responseBuffer.Clear();

	if ( m_parameterBuffer.Size() < ExpectedCommandParameters[ static_cast<size_t>( command ) ] )
	{
		dbLogWarning( "CDRomDrive::ExecuteCommand() -- Wrong number of parameters" );
		SendError( ErrorCode::WrongNumberOfParameters );
		return;
	}

	switch ( command )
	{
		// Control commands

		case Command::SetFilter:
		{
			// Automatic ADPCM (CD-ROM XA) filter ignores sectors except those which have the same channel and file numbers in their subheader.
			// This is the mechanism used to select which of multiple songs in a single .XA file to play.
			m_xaFilter.file = m_parameterBuffer.Pop();
			m_xaFilter.channel = m_parameterBuffer.Pop();
			m_xaFilter.set = true;
			SendResponse();
			break;
		}

		case Command::SetMode:
		{
			// The "Ignore Bit" does reportedly force a sector size of 2328 bytes (918h), however, that doesn't seem to be true. Instead, Bit4 seems to cause the controller to ignore the sector size in Bit5
			// (instead, the size is kept from the most recent Setmode command which didn't have Bit4 set). Also, Bit4 seems to cause the controller to ignore the <exact> Setloc position
			// (instead, data is randomly returned from the "Setloc position minus 0..3 sectors"). And, Bit4 causes INT1 to return status.Bit3=set (IdError). Purpose of Bit4 is unknown?
			m_mode.value = m_parameterBuffer.Pop();
			dbLogDebug( "CDRomDrive::ExecuteCommand -- SetMode [cdda=%i, autoPause: %i, report=%i, xaFilter=%i, ignore=%i, sectorSize=%i, xaadpcm=%i, doubleSpeed=%i]",
				m_mode.cdda, m_mode.autoPause, m_mode.report, m_mode.xaFilter, m_mode.ignoreBit, m_mode.sectorSize, m_mode.xaadpcm, m_mode.doubleSpeed );

			// TODO: handle speed change

			SendResponse();
			break;
		}

		case Command::Init:
		{
			dbLogDebug( "CDRomDrive::ExecuteCommand -- Init" );
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

			dbLogDebug( "CDRomDrive::ExecuteCommnand -- reset" );
			SendResponse();

			m_secondResponseCommand = std::nullopt;
			m_secondResponseEvent->Cancel();

			m_driveState = DriveState::Idle;
			m_driveEvent->Cancel();

			m_driveStatus.value = 0;
			m_driveStatus.motorOn = ( m_cdrom != nullptr );

			m_mode.value = 0;
			m_mode.sectorSize = true;

			m_pendingRead = false;
			m_pendingSeek = false;

			m_muted = false;
			m_muteADPCM = false;

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
			dbLogDebug( "CDRomDrive::EecuteCommand -- motor on" );
			if ( m_driveStatus.motorOn )
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
			dbLogDebug( "CDRomDrive::ExecuteCommand -- stop" );
			const uint32_t stopCycles = m_driveStatus.motorOn ? ( m_mode.doubleSpeed ? 25000000 : 13000000 ) : 7000;
			StopMotor();
			SendResponse();
			QueueSecondResponse( Command::Stop, stopCycles );
			break;
		}

		case Command::Pause:
		{
			// Aborts Reading and Playing, the motor is kept spinning, and the drive head maintains the current location within reasonable error
			dbLogDebug( "CDRomDrive::ExecuteCommand -- pause" );

			// send first response before clearing status bits
			SendResponse();

			m_driveState = DriveState::Idle;
			m_driveEvent->Cancel();

			m_driveStatus.read = false;
			m_driveStatus.play = false;
			m_driveStatus.seek = false;
			
			QueueSecondResponse( Command::Pause );
			break;
		}

		// Seek commands

		case Command::SetLoc:
		{
			const auto mm = m_parameterBuffer.Pop();
			const auto ss = m_parameterBuffer.Pop();
			const auto sect = m_parameterBuffer.Pop();
			dbLogDebug( "CDRomDrive::SetLoc -- amm: %X, ass: %X, asect: %X", mm, ss, sect );

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
			dbLogDebug( "CDRomDrive::ExecuteCommand -- %s", logical ? "SeekL" : "SeekP" );

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
			dbLogDebug( "CDRomDrive::ExecuteCommand -- SetSession" );
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
			dbLogDebug( "CDRomDrive::ExecuteCommand -- %s", ( command == Command::ReadN ) ? "ReadN" : "ReadS" );
			if ( !CanReadDisk() )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				SendResponse();
				if ( !m_pendingSeek && ( m_driveState == DriveState::Reading || ( IsSeeking() && m_pendingRead ) ) )
				{
					dbLogWarning( "CDRomDRive::ExecuteCommand -- already reading" );
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
			m_driveStatus.shellOpen = false;

			break;
		}

		case Command::GetParam:
		{
			dbLogDebug( "CDRomDrive::GetParam" );
			SendResponse();
			m_responseBuffer.Push( m_mode.value );
			m_responseBuffer.Push( 0 ); // always zero
			m_responseBuffer.Push( m_xaFilter.file );
			m_responseBuffer.Push( m_xaFilter.channel );
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
			dbLogDebug( "CDRomDrive::GetLocL" );

			if ( !m_currentSectorHeaders.has_value() )
			{
				SendError( ErrorCode::CannotRespondYet );
			}
			else
			{
				auto& header = m_currentSectorHeaders->header;
				m_responseBuffer.Push( header.minute );
				m_responseBuffer.Push( header.second );
				m_responseBuffer.Push( header.sector );
				m_responseBuffer.Push( header.mode );

				auto& subHeader = m_currentSectorHeaders->subHeader;
				m_responseBuffer.Push( subHeader.file );
				m_responseBuffer.Push( subHeader.channel );
				m_responseBuffer.Push( subHeader.subMode.value );
				m_responseBuffer.Push( subHeader.codingInfo.value );

				m_interruptFlags = InterruptResponse::First;
			}
			break;
		}

		case Command::GetLocP:
		{
			// Retrieves 8 bytes of position information from Subchannel Q with ADR=1.
			// Mainly intended for displaying the current audio position during Play. All results are in BCD.
			dbLogDebug( "CDRomDrive::GetLocP" );

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
			dbLogDebug( "CDRomDrive::ExecuteCommand -- GetTrackNumber" );

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
				dbLogDebug( "CDRomDrive::ExecuteCommand -- GetTD [%X]", (uint32_t)track );

				// TODO: send error if track count is out of range

				// TODO: get location of track from cue sheet

				SendResponse();

				// send bad track location for now
				m_responseBuffer.Push( 0x99 ); // mm
				m_responseBuffer.Push( 0x99 ); // ss
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
			dbLogDebug( "CDRomDrive::ExecuteCommand -- GetID" );
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
			dbLogDebug( "CDRomDrive::ExecuteCommand -- Mute" );
			SendResponse();
			m_muted = true;
			break;
		}

		case Command::Demute:
		{
			// Turn on audio streaming to SPU (affects both CD-DA and XA-ADPCM). The Demute command is needed only if one has formerly used the Mute command
			// (by default, the PSX is demuted after power-up (...and/or after Init command?), and is demuted after cdrom-booting).
			dbLogDebug( "CDRomDrive::ExecuteCommand -- Demute" );
			SendResponse();
			m_muted = false;
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
			dbLogDebug( "CDRomDrive::ExecuteCommand -- Play" );
			uint8_t track = 0;
			if ( !m_parameterBuffer.Empty() )
				track = m_parameterBuffer.Pop();

			SendResponse();
			BeginPlaying( track );
			break;
		}

		case Command::Forward:
		{
			dbLogDebug( "CDRomDrive::ExecuteCommand -- Forward" );
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
			dbLogDebug( "CDRomDrive::ExecuteCommand -- Backward" );
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
			dbLogDebug( "CDRomDRive::Test" );
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
}

#undef COMMAND_CASE

void CDRomDrive::ExecuteCommandSecondResponse() noexcept
{
	dbAssert( m_secondResponseCommand.has_value() );
	const Command command = *m_secondResponseCommand;
	m_secondResponseCommand = std::nullopt;

	dbLogDebug( "CDRomDrive::ExecuteCommandSecondResponse() -- [%X]", command );

	dbAssert( m_queuedInterrupt == 0 ); // cannot queue more than 1 interrupt
	m_secondResponseEvent->Cancel();
	m_secondResponseBuffer.Clear();

	switch ( command )
	{
		case Command::GetID:
		{
			dbLogDebug( "CDRomDrive::ExecuteCommandSecondResponse -- GetID" );
			m_driveStatus.read = false;
			m_driveStatus.seek = false;
			m_driveStatus.play = false;
			m_driveStatus.motorOn = ( m_cdrom != nullptr );

			if ( CanReadDisk() )
			{
				dbLogDebug( "CDRomDrive::ExecuteCommandSecondResponse -- SCEA" );
				static const uint8_t LicensedResponse[]{ 0x02, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A' };
				m_secondResponseBuffer.Push( LicensedResponse, 8 );
				m_queuedInterrupt = InterruptResponse::Second;
			}
			else
			{
				dbLogDebug( "CDRomDrive::ExecuteCommandSecondResponse -- no disk" );
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

	UpdateStatus();
}

void CDRomDrive::ExecuteDriveState() noexcept
{
	const DriveState state = std::exchange( m_driveState, DriveState::Idle );
	switch ( state )
	{
		case DriveState::Idle:
			dbBreak(); // this should not happen
			break;

		case DriveState::StartingMotor:
		{
			dbLogDebug( " CDRomDrive::ExecuteDriveState -- motor on complete" );
			m_driveStatus.read = false;
			m_driveStatus.seek = false;
			m_driveStatus.play = false;
			m_driveStatus.motorOn = true;
			break;
		}

		case DriveState::Seeking:
		{
			dbLogDebug( "CDRomDrive::ExecuteDriveState -- seek complete" );

			// TODO: process sector header

			m_driveStatus.seek = false;
			m_driveStatus.motorOn = true;

			if ( m_pendingRead )
			{
				BeginReading();
			}
			else if ( m_pendingPlay )
			{
				BeginPlaying( m_track );
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
			dbLogDebug( "CDRomDrive::ExecuteDriveState -- read complete" );

			CDRom::Sector sector;
			if ( !m_cdrom->ReadSector( sector ) )
			{
				dbLogWarning( "CDRomDrive::ExecuteDriveState -- Reading from end of disk" );
				// TODO: send end data response (also for read?)
				break;
			}

			ScheduleDriveEvent( state, GetReadCycles() );

			// TODO: figure out what kind of sector it is first

			if ( state == DriveState::Playing || ( state == DriveState::Reading && m_mode.cdda ) )
			{
				// play CD-DA audio
				dbLogDebug( "Ignoring CD-DA sector" );
				if ( m_mode.report )
				{
					m_secondResponseBuffer.Push( m_driveStatus.value );
					m_secondResponseBuffer.Push( m_track ); // track
					m_secondResponseBuffer.Push( m_trackIndex ); // index
					m_secondResponseBuffer.Push( 0 ); // mm/amm
					m_secondResponseBuffer.Push( 2 ); // ss+0x80/ass
					m_secondResponseBuffer.Push( 0 ); // sect/asect
					m_secondResponseBuffer.Push( 0 ); // peaklo
					m_secondResponseBuffer.Push( 0 ); // peakhi
					m_queuedInterrupt = InterruptResponse::ReceivedData;
				}
				break;
			}

			m_currentSectorHeaders = SectorHeaders{ sector.header, sector.mode2.subHeader };

			if ( m_mode.xaadpcm && ( sector.header.mode == 2 ) )
			{
				if ( sector.mode2.subHeader.subMode.audio && sector.mode2.subHeader.subMode.realTime )
				{
					// read XA-ADPCM
					// dbLogDebug( "Ignoring XA-ADPCM sector" );
					DecodeAdpcmSector( sector );
					break;
				}
			}

			m_writeSectorBuffer = ( m_writeSectorBuffer + 1 ) % NumSectorBuffers;
			auto& buffer = m_sectorBuffers[ m_writeSectorBuffer ];

			if ( buffer.size > 0 )
				dbLogWarning( "CDRomDrive::ExecuteDriveState -- overwriting buffer [%u]", m_writeSectorBuffer );

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

					default:	dbBreak();											break;
				}
				buffer.size = CDRom::DataBytesPerSector;
			}

			// TODO: interrupt retry
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

void CDRomDrive::RequestData() noexcept
{
	if ( !m_dataBuffer.Empty() )
	{
		dbLogDebug( "CDRomDrive::RequestData -- data buffer is not empty yet [%u]", m_dataBuffer.Size() );
		return;
	}

	auto& sector = m_sectorBuffers[ m_readSectorBuffer ];

	if ( sector.size > 0 )
	{
		dbLogDebug( "CDRomDrive::RequestData -- loaded %u bytes from buffer %u", sector.size, m_readSectorBuffer );
		m_dataBuffer.Push( sector.bytes.data(), sector.size );
		sector.size = 0;
	}
	else
	{
		dbLogWarning( "CDRomDrive::RequestData -- reading from empty sector buffer" );
		m_dataBuffer.Push( sector.bytes.data(), DataBufferSize );
	}

	// the PSX skips all unprocessed sectors and jumps straight to the newest sector

	auto& nextSector = m_sectorBuffers[ m_writeSectorBuffer ];
	if ( nextSector.size > 0 )
	{
		dbLogDebug( "CDRomDrive::RequestData -- sending additional interrupt for missed sector" );
		SendSecondResponse( InterruptResponse::ReceivedData );
		if ( m_interruptFlags == 0 )
			ShiftQueuedInterrupt();
	}
}

void CDRomDrive::DecodeAdpcmSector( const CDRom::Sector& sector )
{
	auto& subHeader = sector.mode2.subHeader;

	// check XA filter
	if ( ( m_xaFilter.set || m_mode.xaFilter ) && ( subHeader.file != m_xaFilter.file || subHeader.channel != m_xaFilter.channel ) )
	{
		dbLogDebug( "CDRomDrive::DecodeAdpcmSector -- Skipping sector sue to mismatched filter" );
		return;
	}

	// set the XA filter automatically from the current track
	if ( !m_xaFilter.set )
	{
		if ( subHeader.channel == 0xff && ( !m_mode.xaFilter || m_xaFilter.channel != 0xff ) )
		{
			dbLogWarning( "CDRomDrive::DecodeAdpcmSector -- Skipping XA file" );
			return;
		}

		m_xaFilter.file = subHeader.file;
		m_xaFilter.channel = subHeader.channel;
		m_xaFilter.set = true;
	}

	// reset current file on EOF, and play the file in the next sector
	if ( subHeader.subMode.endOfFile )
		m_xaFilter = XaFilter{};

	CDXA::DecodeAdpcmSector( subHeader, sector.mode2.form2.data.data(), m_oldXaAdpcmSamples.data(), m_xaAdpcmSampleBuffer.get() );

	if ( m_muted || m_muteADPCM )
		return;

	const uint32_t sampleCount = ( subHeader.codingInfo.bitsPerSample ? CDXA::AdpcmSamplesPerSector8Bit : CDXA::AdpcmSamplesPerSector4Bit ) /
		( subHeader.codingInfo.stereo ? 2 : 1 );

	if ( subHeader.codingInfo.stereo )
	{
		if ( subHeader.codingInfo.sampleRate )
			ResampleXaAdpcm<true, true>( m_xaAdpcmSampleBuffer.get(), sampleCount );
		else
			ResampleXaAdpcm<true, false>( m_xaAdpcmSampleBuffer.get(), sampleCount );
	}
	else
	{
		if ( subHeader.codingInfo.sampleRate )
			ResampleXaAdpcm<false, true>( m_xaAdpcmSampleBuffer.get(), sampleCount );
		else
			ResampleXaAdpcm<false, false>( m_xaAdpcmSampleBuffer.get(), sampleCount );
	}
}


template <bool IsStereo, bool HalfSampleRate>
void CDRomDrive::ResampleXaAdpcm( const int16_t* samples, uint32_t count )
{
	auto& leftRing = m_resampleRingBuffers[ 0 ];
	[[maybe_unused]] auto& rightRing = m_resampleRingBuffers[ 1 ];

	uint8_t p = m_resampleP; // make local copy for fast access during resampling

	// sixStep shouldn't need to be a member since we are always given samples by a multiple of 6
	dbAssert( ( count % 6 ) == 0 );
	uint8_t sixStep = 0;

	for ( uint32_t i = 0; i < count; ++i )
	{
		const int16_t leftSample = *( samples++ );
		const int16_t rightSample = IsStereo ? *( samples++ ) : leftSample;

		for ( uint32_t dup = 0; dup < ( HalfSampleRate ? 2 : 1 ); ++dup )
		{
			leftRing[ p ] = leftSample;
			if constexpr ( IsStereo )
				rightRing[ p ] = rightSample;

			p = ( p + 1 ) % ResampleRingBufferSize;

			if ( ++sixStep == 6 )
			{
				sixStep = 0;
				for ( uint32_t j = 0; j < 7; ++j )
				{
					const int16_t leftResult = ZigZagInterpolate( leftRing.data(), XaAdpcmZigZagTables[ j ].data(), p );
					const int16_t rightResult = IsStereo ? ZigZagInterpolate( rightRing.data(), XaAdpcmZigZagTables[ j ].data(), p ) : leftResult;
					AddAudioFrame( leftResult, rightResult );
				}
			}
		}
	}

	m_resampleP = p;
}

// CDROM control commands

}