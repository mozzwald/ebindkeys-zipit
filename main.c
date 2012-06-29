/*
	Keyboard shortcut daemon.
	Via evdev module (/dev/input/eventX)
*/

#define _GNU_SOURCE
#define _XOPEN_SOURCE
#define UINPUT_DEV_NAME "ebindkeys-uinput"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <linux/input.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <inttypes.h>
#include <sys/mman.h>

#include "confuse.h"
#include "ebindkeys.h"

#define MAP_SIZE 4096UL

#define GPIO 98	/* lid switch */
#define GPIO_BASE 0x40E00000 /* PXA270 GPIO Register Base */

typedef unsigned long u32;

int regoffset(int gpio) {
	if (gpio < 32) return 0;
	if (gpio < 64) return 4;
	if (gpio < 96) return 8;
	return 0x100;
}

int gpio_read(void *map_base, int gpio) {
	volatile u32 *reg = (u32*)((u32)map_base + regoffset(gpio));
	return (*reg >> (gpio&31)) & 1;
}

#define EBK_KEY_UP 		0
#define EBK_KEY_DOWN 	1

#define LID_CLOSED  0
#define LID_OPEN    1
#define LID_UNKNOWN 255
int lidstate() {
	int fd;
	int retval;
	void *map_base;

	fd = open("/dev/mem", O_RDONLY | O_SYNC);
   	if (fd < 0) {printf("Please run as root"); exit(1);}

    	map_base = mmap(0, MAP_SIZE, PROT_READ, MAP_SHARED, fd, GPIO_BASE);
	if(map_base == (void *) -1) exit(255);

	switch(gpio_read(map_base,98))
	{
		case 0: /* lid is closed */
			retval = LID_CLOSED;
			break;

		case 1: /* lid is open */
			retval = LID_OPEN;
			break;

		default:
			retval = LID_UNKNOWN;
	}

	if(munmap(map_base, MAP_SIZE) == -1) exit(255) ;
	close(fd);
	return retval;
}

void keys_on() {	//turns backlight power on or off

	FILE *key = fopen("/sys/class/backlight/pwm-backlight.1/bl_power", "w");

	//WARNING: opposite of what you might expect - 1 is off and 0 is on
	if (key != NULL) {
		fputs("0", key);
		fclose(key);
	}
}


void onKeyPress() {	//reset timer in /tmp/keytimer

	FILE *key = fopen("/tmp/keypressed", "w");

	if (key != NULL) {
		fputs("1", key);
		fclose(key);
	}

	//now turn on the lights
	keys_on();

}


#define EMU_NAME_KBD "ebindkeys-kdb"
#define EMU_NAME_MOUSE "ebindkeys-mouse"

int openUInput(int *ufile, int *ufile_mouse, int *eventfh, const char* device){

	if (! (*eventfh = open(device , O_RDONLY )))
	{
		fprintf(stderr, "Error opening event device %s", device);
		return 0;
	}

	/* Get exclusive access to the input device so we
	 * can ignore keypresses when lid is closed */
	int result = 0;
	char name[256] = "Unknown";

	result = ioctl(*eventfh, EVIOCGNAME(sizeof(name)), name);
	result = ioctl(*eventfh, EVIOCGRAB, 1);

	/* Setup uinput device for writing */
	int res;
	struct uinput_user_dev uinp;
	*ufile = open("/dev/input/uinput", O_WRONLY);
	if (*ufile == -1)
		*ufile = open("/dev/uinput", O_WRONLY);
	if (*ufile == -1)
	{
		fprintf(stderr, "Error opening uinput device! Is the uinput module loaded?");
		return 0;
	}
	
	

	*ufile_mouse = open("/dev/input/uinput", O_WRONLY);
	if (*ufile_mouse == -1)
		*ufile_mouse = open("/dev/uinput", O_WRONLY);
	if (*ufile_mouse == -1)
	{
		fprintf(stderr, "Error opening uinput mouse device! Is the uinput module loaded?");
		return 0;
	}
	
	ioctl(*ufile, UI_SET_EVBIT, EV_KEY);
	ioctl(*ufile, UI_SET_EVBIT, EV_REL);
	for (int i = 0; i < KEY_MAX; i++)
		ioctl(*ufile, UI_SET_KEYBIT, i);


	/* Mouse events for mouse device */
	ioctl(*ufile_mouse, UI_SET_EVBIT, EV_KEY);
	ioctl(*ufile_mouse, UI_SET_EVBIT, EV_REL);
	ioctl(*ufile_mouse, UI_SET_RELBIT, REL_X);
	ioctl(*ufile_mouse, UI_SET_RELBIT, REL_Y);
	ioctl(*ufile_mouse, UI_SET_KEYBIT, BTN_MOUSE);
	ioctl(*ufile_mouse, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(*ufile_mouse, UI_SET_KEYBIT, BTN_RIGHT);
	ioctl(*ufile_mouse, UI_SET_KEYBIT, BTN_MIDDLE);


	memset(&uinp, 0, sizeof(uinp));
	uinp.id.version = 1;
	uinp.id.bustype = BUS_USB;
	strncpy(uinp.name, EMU_NAME_KBD, sizeof(EMU_NAME_KBD));
//	strncpy(uinp.name, UINPUT_DEV_NAME, sizeof(UINPUT_DEV_NAME));
	res = write(*ufile, &uinp, sizeof(uinp));
	if (res == -1)
	{
		fprintf(stderr, "Error setting up uinput device!");
		return 0;
	}

	memset(&uinp, 0, sizeof(uinp));
	uinp.id.version = 4;
	uinp.id.bustype = BUS_USB;
	strncpy(uinp.name, EMU_NAME_MOUSE, sizeof(EMU_NAME_MOUSE));
	res = write(*ufile_mouse, &uinp, sizeof(uinp));
	if (res == -1)
	{
		fprintf(stderr, "Error setting up uinput mouse device!");
		return 0;
	}



	if (ioctl(*ufile, UI_DEV_CREATE) < 0)
	{
		fprintf(stderr, "Error creating uinput device!");
		return 0;
	}

	if (ioctl(*ufile_mouse, UI_DEV_CREATE) < 0)
	{
		fprintf(stderr, "Error creating uinput device!");
		return 0;
	}
	
	return 1;
}



int send_event(int ufile, __u16 type, __u16 code, __s32 value)
{
	struct input_event event;

	memset(&event, 0, sizeof(event));
	event.type = type;
	event.code = code;
	event.value = value;

	if (write(ufile, &event, sizeof(event)) != sizeof(event)) {
		printf("Error during event sending");
		return -1;
	}

	return 0;
}


#define MAX_ACCEL 24
#define ACCEL_DIVIDOR 3

int process_mouse_event(int ufile_mouse, const struct input_event const* pEvent)
{
	int dx=0; 
	int dy=0;
	static int moving, accel;

	uint32_t code;
	int bEventHandled = 1;
	
	switch(pEvent->code){

		case KEY_UP:

			if (pEvent->value == 0)
				moving--;
			else {//if (pEvent->value == 1)
				moving++;
				dy=-1;
			}
			break;

		case KEY_DOWN:
			if (pEvent->value == 0)
				moving--;
			else{//if (pEvent->value == 1)
				moving++;
				dy=1;
			}
			break;
			
		case KEY_RIGHT:
			if (pEvent->value == 0)
				moving--;
			else{//if (pEvent->value == 1)
				moving++;
				dx=1;
			}
			break;
			
		case KEY_LEFT:
			if (pEvent->value == 0)
				moving--;
			else{//if (pEvent->value == 1)
				moving++;
				dx=-1;
			}
			break;
			
		case KEY_END: //} else if (pEvent->code == lbutton_code {
			send_event(ufile_mouse, EV_KEY, BTN_LEFT, pEvent->value);
			send_event(ufile_mouse, EV_SYN, SYN_REPORT, 0);
			break;
			
		case KEY_STOPCD: //} else if (pEvent->code == rbutton_code) {
			send_event(ufile_mouse, EV_KEY, BTN_RIGHT, pEvent->value);
			send_event(ufile_mouse, EV_SYN, SYN_REPORT, 0);
			break;
			
		case KEY_PLAYCD: //} else if (pEvent->code == mbutton_code) {
			send_event(ufile_mouse, EV_KEY, BTN_MIDDLE, pEvent->value);
			send_event(ufile_mouse, EV_SYN, SYN_REPORT, 0);
			break;
	
		default:
			bEventHandled = 0;	
			
			
	}

	
	/* Clamp value */
	moving = moving < 0 ? 0 : moving;
	moving = moving > 4 ? 4 : moving;

	if (moving) {
		if (accel < MAX_ACCEL)
			accel++;

		send_event(ufile_mouse, EV_REL, REL_X, dx * (1 + accel / ACCEL_DIVIDOR));
		send_event(ufile_mouse, EV_REL, REL_Y, dy * (1 + accel / ACCEL_DIVIDOR));
		send_event(ufile_mouse, EV_SYN, SYN_REPORT, 0);
	} else
		accel = 0;
	
	return bEventHandled;
}

static int bProcessMouse = 0;
static int bTempProcessMouse = 0;

int filterKeyStroke(int ufile, int ufile_mouse, const struct input_event const* pEvent, int bExperiment)
{
	/* this function determines if this keystroke shouild be eaten, injected with alternate keystrokes,
		or just passed on to the rest of the system (i.e. the keymap)	*/

	int bFiltered = 1;
	bTempProcessMouse = 0;
	
	struct input_event 	ievent = *pEvent;

	switch(pEvent->code){
//	case KEY_POWER:

	case KEY_OPTION:
		if(bExperiment){
			//send the <ctrl> keystroke
			ievent.code = KEY_LEFTCTRL;
			write(ufile, &ievent, sizeof(struct input_event));

			//send the <alt> keystroke
			ievent.code = KEY_LEFTALT;
			write(ufile, &ievent, sizeof(struct input_event));

			//send the <F1> keystroke -- it should be assigned to the Home btn in the keymap
			ievent.code = KEY_HOME;
			write(ufile, &ievent, sizeof(struct input_event));


	/********  need to send the Key_Up codes to complete the sequence  ******/
			ievent.value = EBK_KEY_UP; //KEY_UP

			//send the <F1> keystroke
			ievent.code = KEY_HOME;
			write(ufile, &ievent, sizeof(struct input_event));

			//send the <ctrl> keystroke
			ievent.code = KEY_LEFTCTRL;
			write(ufile, &ievent, sizeof(struct input_event));

			//send the <alt> keystroke
			ievent.code = KEY_LEFTALT;
			write(ufile, &ievent, sizeof(struct input_event));
		}
		else
			bProcessMouse = !bProcessMouse;
			bFiltered = 0;
		break;

	case KEY_LEFTCTRL:
		bTempProcessMouse = 1;
		bFiltered = 0;
		
		break;

	case KEY_HOME:
		/* 	*****    do not write the event    ******
			the Home btn should be assigned to F1 in the Keymap
			however, we do not want this keystroke to be called when the button is pressed,
			instead we are only interested in what function ebindkeys would like to run */
		break;

	default:
		bFiltered = 0;
	}


//	if(!bFiltered && (bProcessMouse || bTempProcessMouse))
//		bFiltered = process_mouse_event(ufile_mouse, pEvent);

	return bFiltered;
}

void Match_keysToEvent(key_press* list_start, const event* event_first, int DoNotFork, cfg_bool_t bOnRelease)
//while ( event_cur != NULL && !event_cur->bindToReleaseEvent) /* cycle through all events in the config file*/
{
	int count = list_len(list_start);
	const event* event_cur = event_first;
	key_press* list_cur;

	while ( event_cur->next != NULL ) //cycle through all events in the config file
	{
		/* don't bother matching keys if the key count doesn't match
		 * the keys pressed count */


		if (bOnRelease == event_cur->bindToReleaseEvent && count == event_cur->key_count)
		{
			int j = 0; /* set flag to 0 */

			/* cycle through all the keys for event_cur */
			for ( int i=0; i < event_cur->key_count; i++)
			{
				list_cur = list_start;

				/* check this event's keys to all currently pressed keys */
				while(list_cur->next != NULL)
				{
					if ( event_cur->keys[i] == list_cur->code )
						j++;
					list_cur = list_cur->next;
				}
			}
			if (j == event_cur->key_count)
			{
				if (!strcmp(event_cur->action, "TOGGLE"))
					active ^= 1;
				else if (active)
				{
				/* we have a go. fork and run the action */
					if(DoNotFork)
						system(event_cur->action);
					else if (!fork())
					{
						system(event_cur->action);
						exit(0);
					}

				}
			}
		}

		event_cur = event_cur->next;
	}
}

/* reference:

 * Event Types:
EV_SYN                  0x00
EV_KEY                  0x01
EV_REL                  0x02
EV_ABS                  0x03
EV_MSC                  0x04
EV_SW                   0x05
EV_LED                  0x11
EV_SND                  0x12
EV_REP                  0x14
EV_FF                   0x15
EV_PWR                  0x16
EV_FF_STATUS            0x17
EV_MAX                  0x1f
EV_CNT                  (EV_MAX+1)

* Value:

Release		0
Key Press	1
Auto Repeat 2

* For key codes see keymap.h or linux/input.h

* input event struct:
	struct timeval time;
	unsigned short type;
	unsigned short code;
	unsigned int value;

 */


int main (int argc, char **argv)
{
	/* generic purpose counter */
	int i, j, count;
	signed char ch;
	unsigned short cmd_opts = 0;
	char *devnode = NULL;

	active = 1; /* must be  set to true to run binds */

	/* default conf_file */
	/* fixme: what if there's no HOME environ var? */
	conf_file = calloc(strlen(getenv("HOME")) + strlen("/.ebindkeysrc"), sizeof(char));
	sprintf(conf_file, "%s/.ebindkeysrc", getenv("HOME"));

	/* work through command line options */
	while( (ch = getopt(argc, argv, "f:dslrhne:")) != -1)
	{
		switch (ch)
		{
			case 'f':
				/* override default conf file */
				free(conf_file);
				conf_file = strdup(optarg);
				break;
			case 'd':
				/* don't fork at startup */
				cmd_opts |= EBK_NODAEMON;
				break;
			case 's':
				/* don't fork when executing actions. */
				cmd_opts |= EBK_NOFORK;
				break;
			case 'l':
				/* list the names of keys */
				break;
			case 'r':
				/* report key presses / releases */
				cmd_opts |= EBK_SHOWKEYS;
				break;
			case 'n':
				devnode = strdup(optarg);
				break;
			case ':':
				exit(1);
				break;
			case 'h':
				break;
			case 'e':
				cmd_opts |= EBK_EXPERIMENTAL;
				break;
			/*default:
				printf("Usage: %s [options]\n", argv[0]);
				exit(1);
				break; */
		}
	}

	/* check if a conf file exists, if not, bitch at user */
	FILE *conf_check;
	if (! (conf_check = fopen(conf_file, "r")) ) // check home or command line dir first
	{
		fprintf(stderr, "%s: could not open config file %s\n", argv[0], conf_file);
		free(conf_file);
		conf_file = "/etc/ebindkeysrc";
		if (! (conf_check = fopen(conf_file, "r")) ) // check etc
		{
			fprintf(stderr, "%s: could not open config file %s\n", argv[0], conf_file);
			exit(2);
		}
		else fclose(conf_check);

	}
	else fclose(conf_check);

	printf("%s: Loaded config file %s\n", argv[0], conf_file);

	settings *conf = load_settings(conf_file);

	/* combine command line options with setting file options.
	 * command line options override conf file */

	conf->opts |= cmd_opts;

	if (devnode != NULL)
		conf->dev = devnode;

	event *event_first = conf->event_first;

	event_list_global = &event_first;

	/* initialize key_press list */
	key_press *list_start = calloc(1,sizeof(key_press));
	list_start->next = NULL;

	/* points to the last struct in the linked list */
	key_press *list_end = list_start;
	key_press *list_cur, *list_prev;

	struct input_event ievent;

	/* No buffering, for now. */
	int eventfh;
	int ufile;
	int ufile_mouse;

	/* open uinput for exclusive access to the input device  */
	if(!openUInput(&ufile, &ufile_mouse, &eventfh, conf->dev))
		exit(3);

	/* How does a good parent prevent his children from becoming
	 * part of the zombie hoard? He ignores them! */
	signal(SIGCHLD, SIG_IGN);

	signal(SIGUSR1, reload_settings);

	if ( ! ( ISSET(conf->opts, EBK_NODAEMON) ) )
		if (fork()) exit(0);


	for(;;)
	{
		if ( read(eventfh, &ievent, sizeof(struct input_event)) == -1 )
		{
			/* read() will always get sizeof(struct input_event) number
			 * of bytes, the kernel gurantees this, so we only worry
			 * about reads error. */

			perror("Error reading device");
			exit(3);
		}

		/* Do nothing if lid is closed */
		if ( lidstate() != LID_CLOSED ) {

			int bFiltered = 0;

			if(bProcessMouse || bTempProcessMouse){
				bFiltered = process_mouse_event(ufile_mouse, &ievent);
					if(bFiltered) continue;
			}
			/* Key has been pressed */
			if ( ievent.type == EV_KEY && ievent.value == EBK_KEY_DOWN )
			{
					if ( ISSET(conf->opts, EBK_SHOWKEYS) ) {
						printf(">%X<\n", ievent.code);
						fflush(stdout);
					}

					/*reset the keyboard timer and turn on the lights */
					onKeyPress();

					/* if no other keys are pressed, filter the keystroke */
					if(list_start->next == NULL)
						bFiltered = filterKeyStroke(ufile, ufile_mouse, &ievent, ISSET(conf->opts, EBK_EXPERIMENTAL));

			//		if(bFiltered) continue;

					/* add to depressed struct */
					list_end->code = ievent.code;
					list_end->next = calloc(1,sizeof(key_press));
					list_end = list_end->next;
					list_end->next = NULL;

					Match_keysToEvent(list_start, event_first, (ISSET(conf->opts, EBK_NOFORK)), cfg_false);
			}

			/* Key has been released */
			if ( ievent.type == EV_KEY && ievent.value == EBK_KEY_UP )
			{
				if ( ISSET(conf->opts,EBK_SHOWKEYS) )
				{
						printf("<%X>\n", ievent.code);
						fflush(stdout);
				}

				Match_keysToEvent(list_start, event_first, (ISSET(conf->opts, EBK_NOFORK)), cfg_true);

				/* remove from depressed struct */
				list_cur = list_start;
				list_prev = NULL;

				while (list_cur->code != ievent.code && list_cur->next != NULL)
				{
					list_prev = list_cur;
					list_cur = list_cur->next;
				}

				/* if the bellow is true, most likely, a key was released
				 * but ebindkeys didn't detect the press */
				if (list_cur->next == NULL)
					continue;


				if (list_prev == NULL)
				{
					/* no previous? we're at start! */
					list_start = list_cur->next;
				}
				else
				{
					list_prev->next = list_cur->next;
				}

				free(list_cur);
			
				if(ievent.code == KEY_LEFTCTRL)
					bTempProcessMouse=0;
			}
			
			if(!bFiltered)
				write(ufile, &ievent, sizeof(struct input_event));
		}
	}

	if(ioctl(ufile, UI_DEV_DESTROY) < 0)
        fprintf(stderr, "Error destroying uinput device!");

	if(ioctl(ufile_mouse, UI_DEV_DESTROY) < 0)
        fprintf(stderr, "Error destroying uinput device!");

	close(eventfh);
	close(ufile);
	close(ufile_mouse);
	return 0;
}


settings *load_settings (const char *conffile)
{
	/* load settings from conffile */

	/* generic purpose counters */
	unsigned int i, j;

	/* declare and initialize the first event */
	event *event_first = calloc(1, sizeof(event));
	event_first->next = NULL;
	event *event_cur = event_first;

	settings *conf = calloc(1, sizeof(settings));

	cfg_opt_t ebk_event_opts[] =
	{
		CFG_INT_LIST("keys", "", CFGF_NONE),
		CFG_STR("action", "", CFGF_NONE),
		CFG_BOOL("onrelease", 0, CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t ebk_opts[] =
	{
		CFG_BOOL("daemon", 1, CFGF_NONE),
		CFG_STR("dev", "", CFGF_NONE | CFGF_NODEFAULT),
		CFG_SEC("event", ebk_event_opts, CFGF_MULTI),
		CFG_END()
	};

	cfg_t *cfg, *cfg_section;

	cfg = cfg_init(ebk_opts, CFGF_NONE);

	if (cfg_parse(cfg, conffile) == CFG_PARSE_ERROR)
		exit(1);

	conf->dev = strdup(cfg_getstr(cfg, "dev"));

	if ( ! cfg_getbool(cfg, "daemon") )
		conf->opts |= EBK_NODAEMON;

	for (i=0; i < cfg_size(cfg, "event"); i++)
	{
		cfg_section = cfg_getnsec(cfg, "event", i);

		/* easy peasy, set the action */
		event_cur->action = strdup(cfg_getstr(cfg_section, "action"));

		/* set the bool that determines if the action should fire on press or release  */
		/* if it's absent the value will be set to cfg_false */
		event_cur->bindToReleaseEvent = cfg_getbool(cfg_section, "onrelease");

		/* set key_count */
		event_cur->key_count = cfg_size(cfg_section, "keys");

		/* set key array */
		event_cur->keys = calloc(sizeof(short), event_cur->key_count);
		for (j=0; j < event_cur->key_count; j++)
		{
			if ( cfg_getnint(cfg_section, "keys", j) == 0 )
			{
				fprintf(stderr, "%s: Invalid key name: %s\n", conffile, cfg_getnstr(cfg_section, "keys", j));
				exit(2);
			}
			event_cur->keys[j] =  cfg_getnint(cfg_section, "keys", j);
		}

		/* prep the next event list item */
		event_cur->next = calloc(1, sizeof(event));
		event_cur = event_cur->next;
		event_cur->next = NULL;
	}


    /*
	 //print the parsed values to another file
    {
        FILE *fp = fopen("/etc/ebindkeys.conf.out", "w");
        cfg_print(cfg, fp);
        fclose(fp);
    }
	*/

	cfg_free(cfg);

	conf->event_first = event_first;

	return(conf);
}


void reload_settings(int sig)
{
	/* SIGUSR1 handler */
	settings *temp;
	temp = load_settings(conf_file);
	free(temp);
	free(*event_list_global);
	*event_list_global = temp->event_first;
}

unsigned int list_len (key_press *list)
{
	/* counts list elements until end of list */
	int i=0;
	while (list->next != NULL)
	{
		i++;
		list = list->next;
	}
	return i;
}
