/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <SDL_main.h>

#include "sys/platform.h"
#include "framework/Licensee.h"
#include "framework/FileSystem.h"
#include "sys/posix/posix_public.h"
#include "sys/sys_local.h"

#include <locale.h>

#ifdef __PSP__
extern "C"
{
    #include <pspkernel.h>

	PSP_MODULE_INFO("dhewm3", 0, 1, 1);
	PSP_HEAP_SIZE_MAX();
	PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

	int callbacks_exit(int, int, void*)
	{
		sceKernelExitGame();

		return 0;
	}
	  
	int callbacks_thread(unsigned int, void*)
	{
		int id;
		
		id = sceKernelCreateCallback("exit_cb", callbacks_exit, NULL);
		sceKernelRegisterExitCallback(id);
		sceKernelSleepThreadCB();
		
		return 0;
	}
	  
	int callbacks_setup()
	{
		int id;
		
		id = sceKernelCreateThread("cb", callbacks_thread, 0x11, 0xFA0, 0, NULL);
		
		if (id >= 0)
		{
		    sceKernelStartThread(id, 0, NULL);
		}
		
		return id;
	}
}
#endif

static char path_argv[MAX_OSPATH];

bool Sys_GetPath(sysPath_t type, idStr &path) {
	const char *s;
	char buf[MAX_OSPATH];
	char buf2[MAX_OSPATH];
	struct stat st;
	size_t len;

	path.Clear();

	switch(type) {
	case PATH_BASE:
#ifdef __PSP__
		path = PSP_DEFAULT_PATH;
		return true;
#else
		if (stat(BUILD_DATADIR, &st) != -1 && S_ISDIR(st.st_mode)) {
			path = BUILD_DATADIR;
			return true;
		}

		common->Warning("base path '" BUILD_DATADIR "' does not exits");

		// fallback to vanilla doom3 install
		if (stat(LINUX_DEFAULT_PATH, &st) != -1 && S_ISDIR(st.st_mode)) {
			common->Warning("using hardcoded default base path");

			path = LINUX_DEFAULT_PATH;
			return true;
		}

		return false;
#endif

	case PATH_CONFIG:
#ifdef __PSP__
		path = PSP_DEFAULT_PATH;
		return true;
#else
		s = getenv("XDG_CONFIG_HOME");
		if (s)
			idStr::snPrintf(buf, sizeof(buf), "%s/dhewm3", s);
		else
			idStr::snPrintf(buf, sizeof(buf), "%s/.config/dhewm3", getenv("HOME"));

		path = buf;
		return true;
#endif

	case PATH_SAVE:
#ifdef __PSP__
		path = PSP_DEFAULT_PATH;
		return true;
#else
		s = getenv("XDG_DATA_HOME");
		if (s)
			idStr::snPrintf(buf, sizeof(buf), "%s/dhewm3", s);
		else
			idStr::snPrintf(buf, sizeof(buf), "%s/.local/share/dhewm3", getenv("HOME"));

		path = buf;
		return true;
#endif

	case PATH_EXE:
#ifdef __PSP__
		path = PSP_DEFAULT_PATH "/EBOOT.PBP";
		return true;
#else
		idStr::snPrintf(buf, sizeof(buf), "/proc/%d/exe", getpid());
		len = readlink(buf, buf2, sizeof(buf2));
		if (len != -1) {
			path = buf2;
			return true;
		}

		if (path_argv[0] != 0) {
			path = path_argv;
			return true;
		}

		return false;
#endif
	}

	return false;
}

/*
===============
Sys_Shutdown
===============
*/
void Sys_Shutdown( void ) {
	Posix_Shutdown();
}

/*
================
Sys_GetSystemRam
returns in megabytes
================
*/
int Sys_GetSystemRam( void ) {
#ifdef __PSP__
	return 64; // slim
#else
	long	count, page_size;
	int		mb;

	count = sysconf( _SC_PHYS_PAGES );
	if ( count == -1 ) {
		common->Printf( "GetSystemRam: sysconf _SC_PHYS_PAGES failed\n" );
		return 512;
	}
	page_size = sysconf( _SC_PAGE_SIZE );
	if ( page_size == -1 ) {
		common->Printf( "GetSystemRam: sysconf _SC_PAGE_SIZE failed\n" );
		return 512;
	}
	mb= (int)( (double)count * (double)page_size / ( 1024 * 1024 ) );
	// round to the nearest 16Mb
	mb = ( mb + 8 ) & ~15;
	return mb;
#endif
}

/*
==================
Sys_DoStartProcess
if we don't fork, this function never returns
the no-fork lets you keep the terminal when you're about to spawn an installer

if the command contains spaces, system() is used. Otherwise the more straightforward execl ( system() blows though )
==================
*/
void Sys_DoStartProcess( const char *exeName, bool dofork ) {
#ifndef __PSP__
	bool use_system = false;
	if ( strchr( exeName, ' ' ) ) {
		use_system = true;
	} else {
		// set exec rights when it's about a single file to execute
		struct stat buf;
		if ( stat( exeName, &buf ) == -1 ) {
			printf( "stat %s failed: %s\n", exeName, strerror( errno ) );
		} else {
			if ( chmod( exeName, buf.st_mode | S_IXUSR ) == -1 ) {
				printf( "cmod +x %s failed: %s\n", exeName, strerror( errno ) );
			}
		}
	}
	if ( dofork ) {
		switch ( fork() ) {
		case -1:
			printf( "fork failed: %s\n", strerror( errno ) );
			break;
		case 0:
			if ( use_system ) {
				printf( "system %s\n", exeName );
				if (system( exeName ) == -1)
					printf( "system failed: %s\n", strerror( errno ) );
				_exit( 0 );
			} else {
				printf( "execl %s\n", exeName );
				execl( exeName, exeName, NULL );
				printf( "execl failed: %s\n", strerror( errno ) );
				_exit( -1 );
			}
			break;
		default:
			break;
		}
	} else {
		if ( use_system ) {
			printf( "system %s\n", exeName );
			if (system( exeName ) == -1)
				printf( "system failed: %s\n", strerror( errno ) );
			else
				sleep( 1 );	// on some systems I've seen that starting the new process and exiting this one should not be too close
		} else {
			printf( "execl %s\n", exeName );
			execl( exeName, exeName, NULL );
			printf( "execl failed: %s\n", strerror( errno ) );
		}
		// terminate
		_exit( 0 );
	}
#endif
}

/*
=================
Sys_OpenURL
=================
*/
void idSysLocal::OpenURL( const char *url, bool quit ) {
	const char	*script_path;
	idFile		*script_file;
	char		cmdline[ 1024 ];

	static bool	quit_spamguard = false;

	if ( quit_spamguard ) {
		common->DPrintf( "Sys_OpenURL: already in a doexit sequence, ignoring %s\n", url );
		return;
	}

	common->Printf( "Open URL: %s\n", url );
	// opening an URL on *nix can mean a lot of things ..
	// just spawn a script instead of deciding for the user :-)

	// look in the savepath first, then in the basepath
	script_path = fileSystem->BuildOSPath( cvarSystem->GetCVarString( "fs_savepath" ), "", "openurl.sh" );
	script_file = fileSystem->OpenExplicitFileRead( script_path );
	if ( !script_file ) {
		script_path = fileSystem->BuildOSPath( cvarSystem->GetCVarString( "fs_basepath" ), "", "openurl.sh" );
		script_file = fileSystem->OpenExplicitFileRead( script_path );
	}
	if ( !script_file ) {
		common->Printf( "Can't find URL script 'openurl.sh' in either savepath or basepath\n" );
		common->Printf( "OpenURL '%s' failed\n", url );
		return;
	}
	fileSystem->CloseFile( script_file );

	// if we are going to quit, only accept a single URL before quitting and spawning the script
	if ( quit ) {
		quit_spamguard = true;
	}

	common->Printf( "URL script: %s\n", script_path );

	// StartProcess is going to execute a system() call with that - hence the &
	idStr::snPrintf( cmdline, 1024, "%s '%s' &",  script_path, url );
	sys->StartProcess( cmdline, quit );
}

/*
===============
main
===============
*/
#ifdef __PSP__
#define SDL_main main
#endif

int main(int argc, char **argv) {
#ifdef __PSP__
	callbacks_setup();
	common->Init( 0, NULL );
#else
	// fallback path to the binary for systems without /proc
	// while not 100% reliable, its good enough
	if (argc > 0) {
		if (!realpath(argv[0], path_argv))
			path_argv[0] = 0;
	} else {
		path_argv[0] = 0;
	}

	// some ladspa-plugins (that may be indirectly loaded by doom3 if they're
	// used by alsa) call setlocale(LC_ALL, ""); This sets LC_ALL to $LANG or
	// $LC_ALL which usually is not "C" and will fuck up scanf, strtod
	// etc when using a locale that uses ',' as a float radix.
	// so set $LC_ALL to "C".
	setenv("LC_ALL", "C", 1);

	if ( argc > 1 ) {
		common->Init( argc-1, &argv[1] );
	} else {
		common->Init( 0, NULL );
	}
#endif

	while (1) {
		common->Frame();
	}
	return 0;
}
