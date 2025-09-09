#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "terminal.h"
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>

static struct termios g_old;
static int g_raw = 0;
volatile sig_atomic_t g_resized = 0;

void term_raw_on(void){
    if(g_raw) return;
    struct termios t;
    tcgetattr(STDIN_FILENO,&g_old);
    t = g_old;
    t.c_lflag &= ~(ICANON|ECHO);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO,TCSANOW,&t);
    g_raw=1;
}
void term_raw_off(void){
    if(!g_raw) return;
    tcsetattr(STDIN_FILENO,TCSANOW,&g_old);
    g_raw=0;
}
void term_hide_cursor(void){ write(STDOUT_FILENO,"\x1b[?25l",6); }
void term_show_cursor(void){ write(STDOUT_FILENO,"\x1b[?25h",6); }
void term_clear(void){ write(STDOUT_FILENO,"\x1b[2J\x1b[H",7); }
void term_move(int row,int col){
    char esc[32];
    int n=snprintf(esc,sizeof(esc),"\x1b[%d;%dH", row, col);
    write(STDOUT_FILENO,esc,n);
}
void term_clear_line(void){ write(STDOUT_FILENO,"\x1b[2K",4); }
void term_alt_on(void){ write(STDOUT_FILENO, "\x1b[?1049h", 8); }
void term_alt_off(void){ write(STDOUT_FILENO, "\x1b[?1049l", 8); }
void term_wrap_off(void){ write(STDOUT_FILENO, "\x1b[?7l", 5); }
void term_wrap_on(void){ write(STDOUT_FILENO, "\x1b[?7h", 5); }

void on_winch(int sig){ (void)sig; g_resized=1; }
void get_tty_size(int *w,int *h){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==0 && ws.ws_col>0 && ws.ws_row>0){ *w=ws.ws_col; *h=ws.ws_row; return; }
    *w=80; *h=24;
}
