#include <stdbool.h>
#include <ppu-lv2.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sys/thread.h>
#include <sys/file.h>
#include <sysutil/msg.h>
#include <sysmodule/sysmodule.h>
#include <NoRSX.h>
#include <NoRSX/NoRSXutil.h>

#include "server.h"
#include "client.h"
#include "command.h"

#include "feat/feat.h"
#include "base/base.h"
#include "ext/ext.h"

#define MOUNT_POINT	"/dev_blind"

using namespace std;

void load_sysmodules(void)
{
	sysModuleLoad(SYSMODULE_FS);
	sysModuleLoad(SYSMODULE_NET);
	sysModuleLoad(SYSMODULE_NETCTL);
	sysModuleLoad(SYSMODULE_SYSUTIL);
	sysModuleLoad(SYSMODULE_FREETYPE_TT);
	
	netInitialize();
	netCtlInit();
}

void unload_sysmodules(void)
{
	netCtlTerm();
	netDeinitialize();

	sysModuleUnload(SYSMODULE_FREETYPE_TT);
	sysModuleUnload(SYSMODULE_SYSUTIL);
	sysModuleUnload(SYSMODULE_NETCTL);
	sysModuleUnload(SYSMODULE_NET);
	sysModuleUnload(SYSMODULE_FS);
}

#ifdef __cplusplus
extern "C" {
#endif

LV2_SYSCALL sysLv2FsMount(const char* name, const char* fs, const char* path, int readonly)
{
	lv2syscall8(837,
		(u64)name,
		(u64)fs,
		(u64)path,
		0, readonly, 0, 0, 0);
	return_to_user_prog(s32);
}

LV2_SYSCALL sysLv2FsUnmount(const char* path)
{
	lv2syscall1(838, (u64)path);
	return_to_user_prog(s32);
}

#ifdef __cplusplus
}
#endif

void server_run_ex(void* arg)
{
	struct Server* ftp_server = (struct Server*) arg;
	uint32_t ret = server_run(ftp_server);
	sysThreadExit(ret);
}

int main(void)
{
	// Initialize required sysmodules.
	load_sysmodules();

	// Initialize framebuffer.
	NoRSX* gfx = new (nothrow) NoRSX();

	if(!gfx)
	{
		// memory allocation error

		// Unload sysmodules.
		unload_sysmodules();

		return -2;
	}

	Background bg(gfx);
	Font text(LATIN2, gfx);

	// netctl variables
	s32 netctl_state;
	net_ctl_info netctl_info;

	// Check network connection status.
	netCtlGetState(&netctl_state);

	if(netctl_state != NET_CTL_STATE_IPObtained)
	{
		MsgDialog md(gfx);
		md.Dialog((msgType) (MSG_DIALOG_NORMAL
			| MSG_DIALOG_BTN_TYPE_OK
			| MSG_DIALOG_DISABLE_CANCEL_ON),
			"A network connection is required to use this application.");
		
		// Free up graphics.
		gfx->NoRSX_Exit();

		// Unload sysmodules.
		unload_sysmodules();

		return -1;
	}

	// Obtain network connection IP address.
	netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &netctl_info);

	// Mount dev_blind.
	sysLv2FsMount("CELL_FS_IOS:BUILTIN_FLSH1", "CELL_FS_FAT", MOUNT_POINT, 0);

	// Create the server thread.
	struct Command ftp_command;

	// initialize command struct
	command_init(&ftp_command);

	// import commands...
	feat_command_import(&ftp_command);
	base_command_import(&ftp_command);
	ext_command_import(&ftp_command);

	struct Server ftp_server;
	
	// initialize server struct
	server_init(&ftp_server, &ftp_command, 2121);

	sys_ppu_thread_t server_tid;
	sysThreadCreate(&server_tid, server_run_ex, (void*) &ftp_server, 1000, 0x10000, THREAD_JOINABLE, (char*) "ftpd");
	sysThreadYield();

	// Start application loop.
	gfx->AppStart();

	int flipped = 0;
	int last_num_connections = 0;

	while(gfx->GetAppStatus())
	{
		int num_connections = ftp_server.nfds - 1;

		if(gfx->GetXMBStatus() == XMB_CLOSE && flipped < 2)
		{
			bg.Mono(COLOR_BLACK);

			text.Printf(100, 100, COLOR_WHITE, "OpenPS3FTP " APP_VERSION);
			text.Printf(100, 150, COLOR_WHITE, "IP Address: %s (port: %d)", netctl_info.ip_address, ftp_server.port);
			text.Printf(100, 200, COLOR_WHITE, "Connections: %d", num_connections);

			flipped++;
			gfx->Flip();
		}
		else
		{
			if(gfx->GetXMBStatus() == XMB_OPEN)
			{
				// flip an extra 4 frames
				flipped = -4;
			}

			if(last_num_connections != num_connections)
			{
				flipped = -2;
				last_num_connections = num_connections;
			}

			waitFlip();
			flip(gfx->context, gfx->currentBuffer);
			gfx->currentBuffer = !gfx->currentBuffer;
			setRenderTarget(gfx->context, &gfx->buffers[gfx->currentBuffer]);
			sysUtilCheckCallback();
		}

		 if(!ftp_server.running)
		 {
			 break;
		 }
	}

	gfx->Flip();

	// Join server thread and wait for exit...
	server_stop(&ftp_server);

	u64 thread_exit;
	sysThreadJoin(server_tid, &thread_exit);

	if(thread_exit != 0 && gfx->GetAppStatus())
	{
		char errmsg[32];
		sprintf(errmsg, "Server exited with error code %d.", (int) thread_exit);

		MsgDialog md(gfx);
		md.Dialog((msgType) (MSG_DIALOG_NORMAL
			| MSG_DIALOG_BTN_TYPE_OK
			| MSG_DIALOG_DISABLE_CANCEL_ON),
			errmsg);
	}

	server_free(&ftp_server);
	command_free(&ftp_command);

	// Unmount dev_blind.
	sysLv2FsUnmount(MOUNT_POINT);

	// Free up graphics.
	gfx->NoRSX_Exit();

	// Unload sysmodules.
	unload_sysmodules();
	
	return 0;
}
