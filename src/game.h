/*
 * game.h - the three phases app_tick drives.
 */

#ifndef IGN_GAME_H
#define IGN_GAME_H

int game_init(void);
int game_frame(void);
int game_shutdown(void);

int game_cd_init(void);
void game_check_cd(void);
void present_back_buffer(void);

extern int g_game_mode;
extern int g_language;

#endif /* IGN_GAME_H */
