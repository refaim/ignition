/*
 * menubg.h - the front end's animated background: TEST.PFM, frames of
 * screen space drawing commands, played in the order DEFAULT.PSQ gives.
 */

#ifndef IGN_MENUBG_H
#define IGN_MENUBG_H

#include "types.h"

int menubg_load_pages(u8 *buffer);
int menubg_load_shades(u8 *buffer);
int menubg_parse(const u8 *pfm, long size);
void menubg_free(void);
int menu_background_render(void);

#endif /* IGN_MENUBG_H */
