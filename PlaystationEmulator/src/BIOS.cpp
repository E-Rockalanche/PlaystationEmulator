#include "BIOS.h"

#include "Instruction.h"

#include <fstream>
#include <string>

namespace PSX
{

bool LoadBios( const char* filename, Bios& bios )
{
	std::ifstream fin( filename, std::ios::binary );
	if ( !fin.is_open() )
		return false;

	fin.seekg( 0, std::ios::end );
	const auto biosSize = fin.tellg();
	if ( biosSize != BiosSize )
		return false;

	fin.seekg( 0, std::ios::beg );

	fin.read( (char*)bios.Data(), BiosSize );
	fin.close();

	// patch BIOS to force TTY output (seems to require proper dual serial port)
	// bios.Write<uint32_t>( 0x1bc3 << 2, 0x24010001 );
	// bios.Write<uint32_t>( 0x1bc5 << 2, 0xaf81a9c0 );

	return true;
}

const char* const FunctionNamesA[]
{
	// 00
	"FileOpen",
	"FileSeek",
	"FileRead",
	"FileWrite",
	"FileClose",
	"FileIoctl",
	"exit",
	"FileGetDeviceFlag",
	"FileGetc",
	"FilePutc",
	"todigit",
	"atof",
	"strtoul",
	"strtol",
	"abs",
	"labs",

	// 10
	"atoi",
	"atol",
	"atob",
	"SaveState",
	"RestoreState",
	"strcat",
	"strncat",
	"strcmp",
	"strncmp",
	"strcpy",
	"strncpy",
	"strlen",
	"index",
	"rindex",
	"strchr",
	"strrchr",

	// 20
	"strpbrk",
	"strspn",
	"strcspn",
	"strtok",
	"strstr",
	"toupper",
	"tolower",
	"bcopy",
	"bzero",
	"bcmp",
	"memcpy",
	"memset",
	"memmove",
	"memcmp",
	"memchr",
	"rand",

	// 30
	"srand",
	"qsort",
	"strtod",
	"malloc",
	"free",
	"lsearch",
	"bsearch",
	"calloc",
	"realloc",
	"InitHeap",
	"SystemErrorExit",
	"std_in_getchar",
	"std_out_putchar",
	"std_in_gets",
	"std_out_puts",
	"printf",

	// 40
	"SystemErrorUnresolvedException",
	"LoadExeHeader",
	"LoadExeFile",
	"DoExecute",
	"FlushCache",
	"init_a0_b0_c0_vectors",
	"GPU_dw",
	"gpu_send_dma",
	"SendGP1Command",
	"GPU_cw",
	"GPU_cwp",
	"send_gpu_linked_list",
	"gpu_abort_dma",
	"GetGPUStatus",
	"gpu_sync",
	nullptr,

	// 50
	nullptr,
	"LoadAndExecute",
	"GetSysSp",
	nullptr,
	"CdInit",
	"_bu_init",
	"CdRemove",
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"dev_tty_init",
	"dev_tty_open",
	"dev_tty_in_out",
	"dev_tty_ioctl",
	"dev_cd_open",

	// 60
	"dev_cd_read",
	"dev_cd_close",
	"dev_cd_firstfile",
	"dev_cd_nextfile",
	"dev_cd_chdir",
	"dev_card_open",
	"dev_card_read",
	"dev_card_write",
	"dev_card_close",
	"dev_card_firstfile",
	"dev_card_nextfile",
	"dev_card_erase",
	"dev_card_undelete",
	"dev_card_format",
	"dev_card_rename",
	nullptr,

	// 70
	"_bu_init",
	"CdInit",
	"CdRemove",
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"CdAsyncSeekL",
	nullptr,
	nullptr,
	nullptr,
	"CdAsyncGetStatus",
	nullptr,
	"CdAsyncReadSector",
	nullptr,

	// 80
	nullptr,
	"CdAsyncSetMode",
	nullptr,
	nullptr,
	nullptr,
	"CdStop",
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,

	// 90
	"CdromIoIrqFunc1",
	"CdromDmaIrqFunc1",
	"CdromIoIrqFunc2",
	"CdromDmaIrqFunc2",
	"CdromGetInt5errCode",
	"CdInitSubFunc",
	"AddCDROMDevice",
	"AddMemCardDevice",
	"AddDuartTtyDevice",
	"AddDummyTtyDevice",
	nullptr,
	nullptr,
	"SetConf",
	"GetConf",
	"SetCdromIrqAutoAbort",
	"SetMemSize",

	// A0
	"WarmBoot",
	"SystemErrorBootOrDiskFailure",
	"EnqueueCdIntr",
	"DequeueCdIntr",
	"CdGetLbn",
	"CdReadSector",
	"CdGetStatus",
	"bu_callback_okay",
	"bu_callback_err_write",
	"bu_callback_err_busy",
	"bu_callback_err_eject",
	"_card_info",
	"_card_async_load_directory",
	"set_card_auto_format",
	"bu_callback_err_prev_write",
	"card_write_test",

	// B0
	nullptr,
	nullptr,
	"ioabort_raw",
	nullptr,
	"GetSystemInfo",
};
static_assert( std::size( FunctionNamesA ) == 0xb5 );

void LogKernalCallA( uint32_t call, uint32_t pc )
{
	auto str = ( call < std::size( FunctionNamesA ) ) ? FunctionNamesA[ call ] : nullptr;
	if ( str )
		std::printf( "A(%02X): %s from %08X\n", call, str, pc );
}

const char* const FunctionNamesB[]
{
	// 00
	"alloc_kernel_memory",
	"free_kernel_memory",
	"init_timer",
	"get_timer",
	"enable_timer_irq",
	"disable_timer_irq",
	"restart_timer",
	"DeliverEvent",
	"OpenEvent",
	"CloseEvent",
	"WaitEvent",
	"TestEvent",
	"EnableEvent",
	"DisableEvent",
	"OpenThread",
	"CloseThread",

	// 10
	"ChangeThread",
	nullptr,
	"InitPad",
	"StartPad",
	"StopPad",
	"OutdatedPadInitAndStart",
	"OutdatedPadGetButtons",
	"ReturnFromException",
	"SetDefaultExitFromException",
	"SetCustomExitFromException",
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,

	// 20
	"UnDeliverEvent",
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,

	// 30
	nullptr,
	nullptr,
	"FileOpen",
	"FileSeek",
	"FileRead",
	"FileWrite",
	"FileClose",
	"FileIoctl",
	"exit",
	"FileGetDeviceFlag",
	"FileGetc",
	"FilePutc",
	"std_in_getchar",
	"std_out_putchar",
	"std_in_gets",
	"std_out_puts",

	// 40
	"chdir",
	"FormatDevice",
	"firstfile",
	"nextfile",
	"FileRename",
	"FileDelete",
	"FileUndelete",
	"AddDevice",
	"RemoveDevice",
	"PrintInstalledDevices",
	"InitCard",
	"StartCard",
	"StopCard",
	"_card_info_subfunc",
	"write_card_sector",
	"read_card_sector",

	// 50
	"allow_new_card",
	"Krom2RawAdd",
	nullptr,
	"Krom2Offset",
	"GetLastError",
	"GetLastFileError",
	"GetC0Table",
	"GetB0Table",
	"get_bu_callback_port",
	"testdevice",
	nullptr,
	"ChangeClearPad",
	"get_card_status",
	"wait_card_status",
};
static_assert( std::size( FunctionNamesB ) == 0x5e );

void LogKernalCallB( uint32_t call, uint32_t pc )
{
	auto str = ( call < std::size( FunctionNamesB ) ) ? FunctionNamesB[ call ] : nullptr;
	if ( str )
		std::printf( "B(%02X): %s from %08X\n", call, str, pc );
}

const char* const FunctionNamesC[]
{
	// 00
	"EnqueueTimerAndVblankIrqs",
	"EnqueueSyscallHandler",
	"SysEnqIntRP",
	"SysDeqIntRP",
	"get_free_EvCB_slot",
	"get_free_TCB_slot",
	"ExceptionHandler",
	"InstallExceptionHandlers",
	"SysInitMemory",
	"SysInitKernelVariables",
	"ChangeClearRCnt",
	nullptr,
	"InitDefInt",
	"SetIrqAutoAck",
	"dev_sio_init",
	"dev_sio_open",

	// 10
	"dev_sio_in_out",
	"dev_sio_ioctl",
	"InstallDevices",
	"FlushStdInOutPut",
	nullptr,
	"tty_cdevinput",
	"tty_cdevscan",
	"tty_circgetc",
	"tty_circputc",
	"ioabort",
	"set_card_find_mode",
	"KernelRedirect",
	"AdjustA0Table",
	"get_card_find_mode",
};
static_assert( std::size( FunctionNamesC ) == 0x1e );

void LogKernalCallC( uint32_t call, uint32_t pc )
{
	auto str = ( call < std::size( FunctionNamesC ) ) ? FunctionNamesC[ call ] : nullptr;
	if ( str )
		std::printf( "C(%02X): %s from %08X\n", call, str, pc );
}

const char* const SystemCallNames[]
{
	"NoFunction",
	"EnterCriticalSection",
	"ExitCriticalSection",
	"ChangeThreadSubFunction"
};

void LogSystemCall( uint32_t arg0, uint32_t pc )
{
	auto str = ( arg0 < std::size( SystemCallNames ) ) ? SystemCallNames[ arg0 ] : "DeliverEvent";
	std::printf( "SYS(%02X): %s from %08X\n", arg0, str, pc );
}


}