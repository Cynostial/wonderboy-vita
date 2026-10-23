#pragma once
#include <stdint.h>
/* Takes a private writable artwork copy; removes its title once, before it
 * can be copied into any display buffer. The packaged artwork is unchanged. */
void wb_setup_screen_art(uint32_t *);
void wb_setup_screen(uint32_t *,const char *,unsigned,unsigned,int);
