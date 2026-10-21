/*
 * game.h - the three phases app_tick drives.
 */

#ifndef IGN_GAME_H
#define IGN_GAME_H

#include "types.h"

int game_init(void);
int game_frame(void);
int game_shutdown(void);

int game_cd_init(void);
void game_check_cd(void);
void present_back_buffer(void);

extern int g_game_mode;
extern int g_language;
extern s32 g_net_state;

extern int g_race_load_stage;   /* 0x553290 */
extern int g_race_running;      /* 0x525e5c */
extern int g_race_ending;       /* 0x563bf4 */
extern int g_race_again;        /* 0x527f70 */
extern s32 g_fps_sum;           /* 0x494988 */
extern s32 g_fps_count;         /* 0x49498c */

#endif /* IGN_GAME_H */
