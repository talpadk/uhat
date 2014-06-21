/*
    uhat - Map joystick hat switch axes to virtual joystick buttons.
    Copyright (C) 2012 Wilmer van der Gaast <wilmer@gaast.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of version 2 of the GNU General Public License
    as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

static int uinp_fd = -1;
struct uinput_user_dev uinp;
int verbose;

struct axis
{
	int number;
	int btn_base;
	int state;
};

void do_axis( struct js_event *ev, struct axis *ax );
void send_event( int btn, int state );

int main( int argc, char *argv[] )
{
	char *dev = "/dev/input/js0";
	struct axis *ax;
	int i, nax, opt, bg = 0;
	
	while( ( opt = getopt( argc, argv, "d:Dv" ) ) != -1 )
	{
		switch( opt )
		{
		case 'd':
			dev = optarg;
			break;
		case 'D':
			bg = 1;
			break;
		case 'v':
			/* Not using this for now. */
			verbose = 1;
			break;
		default:
			return 1;
		}
	}
	
	nax = argc - optind;
	if( nax == 0 )
	{
		printf( "Usage: %s [-d <js device>] [-D] <axes...>\n"
		        "\n"
		        "  -D Run in background\n"
		        "  <axes...> list of axis numbers to be mapped to buttons.\n"
		        "\n"
		        "See README for more information.\n",
		        argv[0] );
		return 1;
	}
	
	ax = calloc( nax, sizeof( struct axis ) );
	for( i = 0; i < nax; i ++ )
	{
		if( sscanf( argv[optind+i], "%d", &ax[i].number ) != 1 )
		{
			fprintf( stderr, "Not a number: %s\n", argv[optind+i] );
			return 1;
		}
		ax[i].btn_base = i * 2;
	}
	
	int js = open( dev, O_RDONLY );
	if( js == -1 )
	{
		perror( "open joystick" );
		return 1;
	}
	
	uinp_fd = open( "/dev/uinput", O_WRONLY | O_NONBLOCK );
	if( uinp_fd == -1 )
	{
		perror( "open uinput" );
		return 1;
	}
	
	/* Define our virtual input device. */
	strcpy( uinp.name, "uhat" );
	uinp.id.version = 4;
	uinp.id.bustype = BUS_USB;
	
	/* It has keys. Keycodes in the BTN_JOYSTICK range will automatically
	   make this a joystick device. */
	ioctl( uinp_fd, UI_SET_EVBIT, EV_KEY );
	for( i = 0; i < ( nax * 2 ); i ++ )
		ioctl( uinp_fd, UI_SET_KEYBIT, BTN_JOYSTICK + i );
	
	/* Write our definition, and create the input device. Stuff will now
	   appear in /dev/input. */
	if( write( uinp_fd, &uinp, sizeof(uinp) ) != sizeof(uinp) || 
	    ioctl( uinp_fd, UI_DEV_CREATE ) != 0 )
	{
		perror( "write/ioctl" );
		return 1;
	}
	
	if( bg )
	{
		if( fork() != 0 )
			return 0;
		
		close( 0 );
		close( 1 );
		close( 2 );
		chdir( "/" );
		setsid();
	}
	
	struct js_event ev;
	while( read( js, &ev, sizeof( ev ) ) == sizeof( ev ) )
	{
		if( ev.type == JS_EVENT_AXIS || ev.type == JS_EVENT_INIT )
		{
			for( i = 0; i < nax; i ++ )
			{
				if( ax[i].number == ev.number )
					do_axis( &ev, ax + i );
			}
		}
	}
	
	perror( "read" );
	return 1;
}

void do_axis( struct js_event *ev, struct axis *ax )
{
	int new = 0;
	
	if( ev->value < 0 )
		new = -1;
	else if( ev->value > 0 )
		new = 1;
	
	if( new == ax->state )
		return;
	
	/* De-press the old state first, if there was any. */
	if( ax->state != 0 )
		send_event( ax->btn_base + ( ax->state < 0 ? 0 : 1 ), 0 );
	/* De-press the old state first, if there was any. */
	if( new != 0 )
		send_event( ax->btn_base + ( new < 0 ? 0 : 1 ), 1 );
	
	ax->state = new;
}

void send_event( int btn, int state )
{
	struct input_event oev;
	
	oev.type = EV_KEY;
	oev.code = BTN_JOYSTICK + btn;
	oev.value = state;
	if( write( uinp_fd, &oev, sizeof( oev ) ) != sizeof( oev ) )
	{
		perror( "write" );
		exit( 1 );
	}
}
