#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "util.h"
#include <time.h>

long clamp_long(long v,long lo,long hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
double clamp(double v,double lo,double hi){ if(v<lo) return lo; if(v>hi) return v>hi?hi:v; return v; }
double now_sec(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec + ts.tv_nsec/1e9; }
void msleep(int ms){ struct timespec ts={ms/1000,(ms%1000)*1000000L}; nanosleep(&ts,NULL); }
