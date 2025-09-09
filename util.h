#ifndef UTIL_H
#define UTIL_H
long clamp_long(long v,long lo,long hi);
double clamp(double v,double lo,double hi);
double now_sec(void);
void msleep(int ms);
#endif
