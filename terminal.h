#ifndef TERMINAL_H
#define TERMINAL_H
#include <signal.h>
void term_raw_on(void);
void term_raw_off(void);
void term_hide_cursor(void);
void term_show_cursor(void);
void term_clear(void);
void term_move(int row,int col);
void term_clear_line(void);
void term_alt_on(void);
void term_alt_off(void);
void term_wrap_off(void);
void term_wrap_on(void);
void on_winch(int sig);
void get_tty_size(int *w,int *h);
extern volatile sig_atomic_t g_resized;
#endif
