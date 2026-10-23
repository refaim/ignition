/*
 * menu_panels.h - the rounded boxes behind the front end's text.
 */

#ifndef IGN_MENU_PANELS_H
#define IGN_MENU_PANELS_H

#include "types.h"

void panels_clear(void);
void panels_apart(int on);
void panel_add(int x, int row, int width, int y, int highlight);
void panels_draw(u8 *dst, int pitch);

#endif /* IGN_MENU_PANELS_H */
