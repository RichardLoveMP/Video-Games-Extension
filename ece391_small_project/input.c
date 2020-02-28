/*									tab:8
 *
 * input.c - source file for input control to maze game
 *
 * "Copyright (c) 2004-2011 by Steven S. Lumetta."
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE AUTHOR OR THE UNIVERSITY OF ILLINOIS BE LIABLE TO 
 * ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL 
 * DAMAGES ARISING OUT  OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, 
 * EVEN IF THE AUTHOR AND/OR THE UNIVERSITY OF ILLINOIS HAS BEEN ADVISED 
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE AUTHOR AND THE UNIVERSITY OF ILLINOIS SPECIFICALLY DISCLAIM ANY 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE 
 * PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND NEITHER THE AUTHOR NOR
 * THE UNIVERSITY OF ILLINOIS HAS ANY OBLIGATION TO PROVIDE MAINTENANCE, 
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS."
 *
 * Author:	    Steve Lumetta
 * Version:	    7
 * Creation Date:   Thu Sep  9 22:25:48 2004
 * Filename:	    input.c
 * History:
 *	SL	1	Thu Sep  9 22:25:48 2004
 *		First written.
 *	SL	2	Sat Sep 12 14:34:19 2009
 *		Integrated original release back into main code base.
 *	SL	3	Sun Sep 13 03:51:23 2009
 *		Replaced parallel port with Tux controller code for demo.
 *	SL	4	Sun Sep 13 12:49:02 2009
 *		Changed init_input order slightly to avoid leaving keyboard
 *              in odd state on failure.
 *	SL	5	Sun Sep 13 16:30:32 2009
 *		Added a reasonably robust direct Tux control for demo mode.
 *	SL	6	Wed Sep 14 02:06:41 2011
 *		Updated input control and test driver for adventure game.
 *	SL	7	Wed Sep 14 17:07:38 2011
 *		Added keyboard input support when using Tux kernel mode.
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <termio.h>
#include <termios.h>
#include <unistd.h>

#include "assert.h"
#include "input.h"

#include "./module/tuxctl-ioctl.h"
#include "./module/mtcp.h"
/* set to 1 and compile this file by itself to test functionality */
#define TEST_INPUT_DRIVER 0

/* set to 1 to use tux controller; otherwise, uses keyboard input */
#define USE_TUX_CONTROLLER 0

#define GENERAL_BITMASK   0x04000000
#define THREE_LED_BITMASK 0x00070000
#define FOUR_LED_BITMASK  0x000F0000
#define SECOND_LAST_BYTE  0x00F0
/* stores original terminal settings */
static struct termios tio_orig;

/* Store the file stream */
static int fd;

/* time constant */
const int ten_min = 10;
const int sixty_min = 60;
const int twlbits = 12;
/* bit shift constant */
/* Store the previous command */
cmd_t pcmd;

/* Store whether I should replace the first command */
int should_replace;

/* declare function */
cmd_t get_tux_command();

/* 
 * init_input
 *   DESCRIPTION: Initializes the input controller.  As both keyboard and
 *                Tux controller control modes use the keyboard for the quit
 *                command, this function puts stdin into character mode
 *                rather than the usual terminal mode.
 *   INPUTS: none
 *   OUTPUTS: none
 *   RETURN VALUE: 0 on success, -1 on failure 
 *   SIDE EFFECTS: changes terminal settings on stdin; prints an error
 *                 message on failure
 */
int
init_input ()
{

	/* Open the serial port */
	fd = open("/dev/ttyS0",O_RDWR | O_NOCTTY);
	int ldisc_num = N_MOUSE;
	ioctl(fd,TIOCSETD,&ldisc_num);
	/* End of the target code */

    struct termios tio_new;

    /*
     * Set non-blocking mode so that stdin can be read without blocking
     * when no new keystrokes are available.
     */
    if (fcntl (fileno (stdin), F_SETFL, O_NONBLOCK) != 0) {
        perror ("fcntl to make stdin non-blocking");
	return -1;
    }

    /*
     * Save current terminal attributes for stdin.
     */
    if (tcgetattr (fileno (stdin), &tio_orig) != 0) {
	perror ("tcgetattr to read stdin terminal settings");
	return -1;
    }

    /*
     * Turn off canonical (line-buffered) mode and echoing of keystrokes
     * to the monitor.  Set minimal character and timing parameters so as
     * to prevent delays in delivery of keystrokes to the program.
     */
    tio_new = tio_orig;
    tio_new.c_lflag &= ~(ICANON | ECHO);
    tio_new.c_cc[VMIN] = 1;
    tio_new.c_cc[VTIME] = 0;
    if (tcsetattr (fileno (stdin), TCSANOW, &tio_new) != 0) {
	perror ("tcsetattr to set stdin terminal settings");
	return -1;
    }

    /* Return success. */
    return 0;
}

static char typing[MAX_TYPED_LEN + 1] = {'\0'};

const char*
get_typed_command ()
{
    return typing;
}

void
reset_typed_command ()
{
    typing[0] = '\0';
}

static int32_t
valid_typing (char c)
{
    /* Valid typing include letters, numbers, space, and backspace/delete. */
    return (isalpha (c) || isdigit (c) || ' ' == c || 8 == c || 127 == c);
}

static void
typed_a_char (char c)
{
    int32_t len = strlen (typing);

    if (8 == c || 127 == c) {
        if (0 < len) {
	    typing[len - 1] = '\0';
	}
    } else if (MAX_TYPED_LEN > len) {
	typing[len] = c;
	typing[len + 1] = '\0';
    }
}

/* 
 * get_command
 *   DESCRIPTION: Reads a command from the input controller.  As some
 *                controllers provide only absolute input (e.g., go
 *                right), the current direction is needed as an input
 *                to this routine.
 *   INPUTS: cur_dir -- current direction of motion
 *   OUTPUTS: none
 *   RETURN VALUE: command issued by the input controller
 *   SIDE EFFECTS: drains any keyboard input
 */
cmd_t 
get_command ()
{
#if (USE_TUX_CONTROLLER == 0) /* use keyboard control with arrow keys */
    static int state = 0;             /* small FSM for arrow keys */
#endif
    static cmd_t command = CMD_NONE;
    cmd_t pushed = CMD_NONE;
    int ch;
//	unsigned long button_stat;
/*	unsigned long button_stat;*/
    /* Read all characters from stdin. */
    while ((ch = getc (stdin)) != EOF) {

	/* Backquote is used to quit the game. */
	if (ch == '`')
	    return CMD_QUIT;
	
#if (USE_TUX_CONTROLLER == 0) /* use keyboard control with arrow keys */
	/*
	 * Arrow keys deliver the byte sequence 27, 91, and 'A' to 'D';
	 * we use a small finite state machine to identify them.
	 *
	 * Insert, home, and page up keys deliver 27, 91, '2'/'1'/'5' and
	 * then a tilde.  We recognize the digits and don't check for the
	 * tilde.
	 */
	switch (state) {
	    case 0:
	        if (27 == ch) {
		    state = 1;
		} else if (valid_typing (ch)) {
		    typed_a_char (ch);
		} else if (10 == ch || 13 == ch) {
		    pushed = CMD_TYPED;
		}
		break;
	    case 1:
		if (91 == ch) {
		    state = 2;
		} else {
		    state = 0;
		    if (valid_typing (ch)) {
			/*
			 * Note that we may be discarding an ESC (27), but
			 * we don't use that as typed input anyway.
			 */
			typed_a_char (ch);
		    } else if (10 == ch || 13 == ch) {
			pushed = CMD_TYPED;
		    }
		}
		break;
	    case 2:
	        if (ch >= 'A' && ch <= 'D') {
		    switch (ch) {
			case 'A': pushed = CMD_UP; break;
			case 'B': pushed = CMD_DOWN; break;
			case 'C': pushed = CMD_RIGHT; break;
			case 'D': pushed = CMD_LEFT; break;
		    }
		    state = 0;
		} else if (ch == '1' || ch == '2' || ch == '5') {
		    switch (ch) {
			case '2': pushed = CMD_MOVE_LEFT; break;
			case '1': pushed = CMD_ENTER; break;
			case '5': pushed = CMD_MOVE_RIGHT; break;
		    }
		    state = 3; /* Consume a '~'. */
		} else {
		    state = 0;
		    if (valid_typing (ch)) {
			/*
			 * Note that we may be discarding an ESC (27) and 
			 * a bracket (91), but we don't use either as 
			 * typed input anyway.
			 */
			typed_a_char (ch);
		    } else if (10 == ch || 13 == ch) {
			pushed = CMD_TYPED;
		    }
		}
		break;
	    case 3:
		state = 0;
	        if ('~' == ch) {
		    /* Consume it silently. */
		} else if (valid_typing (ch)) {
		    typed_a_char (ch);
		} else if (10 == ch || 13 == ch) {
		    pushed = CMD_TYPED;
		}
		break;
	}
#else /* USE_TUX_CONTROLLER */
	// printf("ready to get into ioctl!!!!!!!!!!\n");
	/* Tux controller mode; still need to support typed commands. */
	if (valid_typing (ch)) {
	    typed_a_char (ch);
	} else if (10 == ch || 13 == ch) {
	    pushed = CMD_TYPED;
	}

	/* CRITICAL SECTION ABOUT MY OWN CODES ON TUX COMMAND */

	/* END OF CRITICAL SECTION ABOUT MY OWN CODES ON TUX COMMAND */

#endif /* USE_TUX_CONTROLLER */
    }
    if (pushed == CMD_NONE) {
        command = CMD_NONE;
    }
    return pushed;
}

/* 
 * GTC
 *   DESCRIPTION: get TUX command by external function
 *   INPUTS: none
 *   OUTPUTS: acquired TUX command 
 *   RETURN VALUE: command issued by the input controller
 *   SIDE EFFECTS: may change the recorded previous command
 */
cmd_t 
GTC(){
	cmd_t TC = get_tux_command();
	return TC;
}

/* 
 * 	 get_tux_command
 *   DESCRIPTION: get TUX command by external function
 *   INPUTS: none
 *   OUTPUTS: acquired TUX command 
 *   RETURN VALUE: command issued by the input controller
 *   SIDE EFFECTS: may change the recorded previous command
 */
cmd_t
get_tux_command(){
	should_replace = 1;
	unsigned long button_stat;
	button_stat = 0xFF;
//	printf("ready to get into ioctl!!!!!!!!!!\n");
	ioctl(fd,TUX_BUTTONS,&button_stat);
//	printf("ioctl success!!!!!!!!!!!!!!\n");	
	button_stat &= 0x00FF;
//	printf("%ld\n",button_stat);
	cmd_t TUXC = CMD_NONE;

	if((button_stat & SECOND_LAST_BYTE) != SECOND_LAST_BYTE){

		/* It belongs to rldu */
		if((button_stat & SECOND_LAST_BYTE) == 0xE0){	/*move up*/
			TUXC = CMD_UP;
		}
		if((button_stat & SECOND_LAST_BYTE) == 0xD0){   /* move down */
			TUXC = CMD_DOWN;
		}
		if((button_stat & SECOND_LAST_BYTE) == 0xB0){	/* move left */
			TUXC = CMD_LEFT;
		}
		if((button_stat & SECOND_LAST_BYTE) == 0x70){	/* move right */
			TUXC = CMD_RIGHT;
		}
		pcmd = TUXC;	/* update */
		return TUXC;	/* return the acquired value */
		
	}else{
		/* It belongs to abc */
		if(button_stat == 0xFD){
			TUXC = CMD_MOVE_LEFT;	/* move room left */
		}
		if(button_stat == 0xFB){	/* enter the room */
			TUXC = CMD_ENTER;
		}
		if(button_stat == 0xF7){	/* move room right */
			TUXC = CMD_MOVE_RIGHT;
		}
		
	}
	if(pcmd == TUXC){		/* update the previous command */
		return CMD_NONE;
	}else{
		if(TUXC != CMD_NONE){
			pcmd = TUXC;	/* update the previous command */
		}
	}
	return TUXC;
}


/* 
 * 	 setinputcmd
 *   DESCRIPTION: update the input command by external command
 *   INPUTS: none
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: change the recorded previous command to CMD_NONE
 */
void setinputcmd(){
	pcmd = CMD_NONE; /* change the previous cmd */
	return ;
}

/* 
 * shutdown_input
 *   DESCRIPTION: Cleans up state associated with input control.  Restores
 *                original terminal settings.
 *   INPUTS: none
 *   OUTPUTS: none
 *   RETURN VALUE: none 
 *   SIDE EFFECTS: restores original terminal settings
 */
void
shutdown_input ()
{
    (void)tcsetattr (fileno (stdin), TCSANOW, &tio_orig);
	close(fd);
}


/* 
 * display_time_on_tux
 *   DESCRIPTION: Show number of elapsed seconds as minutes:seconds
 *                on the Tux controller's 7-segment displays.
 *   INPUTS: num_seconds -- total seconds elapsed so far
 *   OUTPUTS: none
 *   RETURN VALUE: none 
 *   SIDE EFFECTS: changes state of controller's display
 */
void
display_time_on_tux (int num_seconds)
{
/*#if (USE_TUX_CONTROLLER != 0)
#error "Tux controller code is not operational yet."
#endif*/
	/* first, split the minute and second*/
	int num_min;	/* (Minute) part of the time */
	int num_sec;	/* (Second) part of the time */
	num_min = num_seconds / sixty_min;
	num_sec = num_seconds % sixty_min;

	if(num_min < 10){
		/* print three digit */
		unsigned long led0; unsigned long led1; unsigned long led2;
		led2 = num_min;
		led1 = (num_sec / ten_min);	/* set the led value (3) */
		led0 = (num_sec % ten_min);
		/* bit shift operation */
		led2 <<= 8;
		led1 <<= 4;

		/* Get the print value */	
		unsigned long print_array;
		print_array = (GENERAL_BITMASK | THREE_LED_BITMASK);	/* bitwise array */
		print_array = (print_array | led2 | led1 | led0 );

		/* Tick it into TUX */
		ioctl(fd, TUX_SET_LED, print_array);
	}else{
		/* print four digit */
		unsigned long led0; unsigned long led1; unsigned long led2; unsigned long led3;
		led3 = (num_min / ten_min);
		led2 = (num_min % ten_min);	/* set the led value (4)*/
		led1 = (num_sec / ten_min);
		led0 = (num_sec % ten_min);

		/* Bit shift Operation */
		led3 <<= twlbits;
		led2 <<= 8;
		led1 <<= 4;
		
		/* Get the print value */
		unsigned long print_array;
		print_array = (GENERAL_BITMASK | FOUR_LED_BITMASK);	/* bitwise array */
		print_array = (print_array | led3 | led2 | led1 | led0);	/* set the print array */

		/* Tick it into TUX */
		ioctl(fd,TUX_SET_LED,print_array);
	}
	return ;
}


#if (TEST_INPUT_DRIVER == 1)
int
main ()
{
    cmd_t last_cmd = CMD_NONE;
    cmd_t cmd;
    static const char* const cmd_name[NUM_COMMANDS] = {
        "none", "right", "left", "up", "down", 
	"move left", "enter", "move right", "typed command", "quit"
    };

    /* Grant ourselves permission to use ports 0-1023 */
    if (ioperm (0, 1024, 1) == -1) {
	perror ("ioperm");
	return 3;
    }
    init_input ();
    while (1) {
        while ((cmd = get_command ()) == last_cmd);
	last_cmd = cmd;
	printf ("command issued: %s\n", cmd_name[cmd]);
	if (cmd == CMD_QUIT)
	    break;
	display_time_on_tux (83);
    }
    shutdown_input ();
    return 0;
}
#endif

void get_button_status(uint8_t* ptr){
	ioctl(fd,TUX_BUTTONS,ptr);
	return ;
}
