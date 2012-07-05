/* ebindkeys.h - holds a few defines for ebindkeys.c
 * 
 * */

#define EBK_NODAEMON 0x01
#define EBK_NOFORK 0x02
#define EBK_SHOWKEYS 0x04
#define EBK_EXPERIMENTAL 0x08

#define ISSET(a,b) (a & b) == b

/* linked list for keys pressed */
typedef struct {
	unsigned short code;
	void *next;
} key_press;

typedef struct {
	unsigned short *keys;
	unsigned short key_count;
	char *action;
	cfg_bool_t bindToReleaseEvent;
	void *next;
} event;

typedef struct {
	char *dev;
	unsigned short opts;
	event *event_first;
} settings;

/* global vars */
event **event_list_global; /* so we can reload event list when we get a sigusr1 */
char *conf_file;
char active;

/* prototypes */
settings *load_settings (const char *conffile);
void reload_settings(int sig);
const char *get_key_name ( unsigned short key );
unsigned short get_key_code (char *name);
unsigned int list_len (key_press *list);

