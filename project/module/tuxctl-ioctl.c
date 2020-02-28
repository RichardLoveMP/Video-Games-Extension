/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define RLDUCBAS_SIZE 8
#define ARRAY_LENGTH 10
#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)

/******************************** Constant ********************************/
const uint8_t lookup_table[16] = {0xE7,0x06,0xCB,0x8F,0x2E,0xAD,0xED,0x86,0xEF,0xAF,0xEE,0x6D,0xE1,0x4F,0xE9,0xE8};
/************************ Global Variable *********************************/
uint8_t tuxctl2button; /* 7 bit button status */  
uint8_t tuxctl2ack; /* set MTCP_ACK value */ 
const int istartpoint = 3;
const int dotshift_startpoint = 24;	/* bits to be shifted if I want to move dot */

/* Function Declaration */
int tuxctl_init(struct tty_struct* tty);
int tuxctl_buttons(struct tty_struct* tty, unsigned long arg);
int tuxctl_setled(struct tty_struct* tty, unsigned long arg);

/* lock implementation */
spinlock_t splock;
/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;

    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

	/* If a is equal to MTCP_ACK */
	if(a == MTCP_ACK){
		/* Make it busy */
		tuxctl2ack = 1;
		return ;
	}

	/* If a is equal to MTCP_BIOC_EVENT */
	if(a == MTCP_BIOC_EVENT){
		
		unsigned long flg;
		/* Right,Left,Down,Up,C,B,A,start */
		uint8_t rlducbas[RLDUCBAS_SIZE];
		uint8_t temp;	/* change the value */
		int i = istartpoint;
		int cnt = 0;	/* count how many button is pressed */
		/* get the rldu*/
		
		for(; i >= 0; i --){
			if( (c & (1 << i)) == 0 ){/* If it is pressed, I need to update the array */
				rlducbas[cnt] = 0;
			}else{
				rlducbas[cnt] = 1;	/* unpressed it */
			}
			cnt ++;
		}
		/* get the cbas */
		for(i = istartpoint; i >=0; i --){
			if( (b & (1 << i)) == 0){	/* If it is pressed, I need to update the array */
				rlducbas[cnt] = 0;
			}else{					/* Set the button */
				rlducbas[cnt] = 1;
			}
			cnt ++;
		}
		/* change the left and down */
		
		temp = rlducbas[1];	/* Switch the left and down button */
		rlducbas[1] = rlducbas[2];
		rlducbas[2] = temp;

		spin_lock_irqsave(&splock,flg);/* When I want to modify the tuxctl2button, I need prevent it from being modified by another therad */		
		tuxctl2button = 0; /* Initialize the button, and I */

		/* get the array into a 8-bit number */
		for(i = 0; i < 8; i++){
			if(rlducbas[i]){	
				tuxctl2button |= (1 << ( (8 - 1) - i));	/* if any button pressed, update it */
			}
		}
		spin_unlock_irqrestore(&splock,flg);	/* When I finished modifying the tuxctl2button, I need to unlock it. */
		return ;
	}

	/* If a is equal to MTCP_RESET */
	if(a == MTCP_RESET){
		tuxctl_init(tty);	/* Initialize tux */
		return ;
	}
    /*printk("packet : %x %x %x\n", a, b, c); */
	return ;
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
	// printk("into ioctl");
    switch (cmd) {
		case TUX_INIT:	/* Call the function which initialize tux */
			return tuxctl_init(tty);
		case TUX_BUTTONS:	/* Call the function which handle the buttons */
			return tuxctl_buttons(tty,arg);
		case TUX_SET_LED:	/* Call the function which handle the LED */
			return tuxctl_setled(tty,arg);
		case TUX_LED_ACK:
			return 0;
		case TUX_LED_REQUEST:
			return 0;
		case TUX_READ_LED:
			return 0;
		default:
			return -EINVAL;
    }
}

/* int tuxctl_init()
 * Initialize the variable associated with the driver.
 * Set the variable in the tux and tick it into TUX 
 * Return 0 for success
 */
int 
tuxctl_init(struct tty_struct* tty){
	/* Firstly, initialize all of the variables related to the driver */
	/* I consider two constants -- 1. MTCP_BIOC_ON 2. MTCP_LED_USR */
	uint8_t para[2];
	//uint8_t para2;
	/* Set the button value to unpressed */
	tuxctl2button = 0xFF;
	/* Set the value of the ack to zero (which is OK)*/
	tuxctl2ack = 0x00;
	/* Set the input value */
	
	para[0] = MTCP_BIOC_ON;
	/* Push it into the tuxctl */
	//tuxctl_ldisc_put(tty,&para1,1);

	para[1] = MTCP_LED_USR;
	/* Push it into the tuxctl */
	tuxctl_ldisc_put(tty,para,2);

	splock = SPIN_LOCK_UNLOCKED;
	/* test */
//	printk("init_success\n");
	/* Success, return zero*/
	return 0;
}

/* int tuxctl_buttons() 
 * Take a pointer to a 32-bit integer. Set the bit of low byte corresponding to the 
 * currently pressed button. 
 * Using unsigned long to cast
 */
int
tuxctl_buttons(struct tty_struct* tty, unsigned long arg){
	unsigned long flg;
	uint8_t* ptr;
	/* Test Copy to User */
	/* If copy is unfinished, return -EINVAL */
	spin_lock_irqsave(&splock,flg);
	
	ptr = &tuxctl2button;
	if(copy_to_user((void *) arg, (void *) ptr, sizeof(long))){	/* copy the argument to user space */
		// printk("control button failed\n");
		spin_unlock_irqrestore(&splock,flg);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&splock,flg);
//	printk("control_button\n");
	/* If success, return 0 */
	return 0;
}

/* int tuxctl_setled()
 * The argument is a 32-bit intenger whose lower [16:0] bit 
 * is related to the hex value, while the [20:17] is related to which LED.
 * while the [27:24] is related to correspoinding decimal points.
 * RETURN VALUE: always return zero
 * SIDE EFFECT: Plugin a lock
 */
int
tuxctl_setled(struct tty_struct* tty, unsigned long arg){
	uint8_t enable_usr_mode;	/* enable user mode */
	uint8_t led_set_array[ARRAY_LENGTH];	/* set led array */
	uint8_t dots;				/* bitwise to confirm if any led have dots */
	uint8_t bitcheck;			/* bitmask of the led status */
	int i;						/* iterator */
	int tot;					/* tot led light */
	uint8_t idx;				/* index in array */
	uint8_t isdot;				/* boolean to determine whether it has dot */

	/* argument */
	/* If the current led status is busy, return 0 */
	if(tuxctl2ack == 0){
		//printk("ack is busy");
		return 0;			/* directly return */
	}

	/* Set the value to zero (It is busy) */
	tuxctl2ack = 0;			/* ack is zero */
	
	enable_usr_mode = MTCP_LED_USR;		/* set user mode */

	/* Set the led to USR mode */
	tuxctl_ldisc_put(tty,&enable_usr_mode,1);
	/* Get the array of the input packet */
	
	/* Set the opcode */
	led_set_array[0] = MTCP_LED_SET;

	/* First, set the bitmask of the led*/
	led_set_array[1] = (uint8_t) ((arg >> 16) & (0xF));

	/* Then, Get the dots of the value */
	 
	dots =  (uint8_t)  (arg >> dotshift_startpoint) & (0xF);
	
	
	bitcheck = 1;
	
	i = 0;
	
	tot = 0;
	for(; i < istartpoint + 1; i ++){
		/* If this bit is not zero, it means this light is on. */
		if( led_set_array[1] & (bitcheck << i) ){
			tot += 1; /* One more light is on */
			/* Get the 0 - 15 value of the bit */
			
			idx = (uint8_t) ((arg >> (i * 4)) & 0x0F);
			
			isdot =(uint8_t) (dots & (bitcheck << i));
			if(isdot == 0){ /* if this bit has dot */
				led_set_array[1 + tot] = lookup_table[idx];
			}else{
				led_set_array[1 + tot] = ( lookup_table[idx] | (1<<4) ); 	/* look up table with the dot */
			}
		}
	}
	/* tick it into buffer */
	tuxctl_ldisc_put(tty,led_set_array,2 + tot);	/* tick it into TUX */

//	printk("setled_success");
	/* Finally, return 0 */
	return 0;
}
