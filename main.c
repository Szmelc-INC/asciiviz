// asciiviz.c — ASCII/ANSI visualizer (UTF-8 palettes + effects + background fill + no-jitter)
// Build: make
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include "util.h"
#include "terminal.h"

#define COL_RESET "\x1b[0m"
#define COL_KEY   "\x1b[1;38;5;208m"   /* orange & bold */
#define COL_NAME  "\x1b[38;5;30m"       /* dark cyan */
#define COL_STATE "\x1b[4;38;5;118m"    /* underline lime green */
#define COL_VALUE "\x1b[1;31m"          /* bright red bold */

/* colors for editor mode (draw & color submodes) */
static const char *COL_DKEY   = "\x1b[1;32m";       /* draw mode key - bright green */
static const char *COL_DNAME  = "\x1b[38;5;240m";   /* draw mode name - grey */
static const char *COL_DVALUE = "\x1b[1;37m";       /* draw mode value - white bold */
static const char *COL_CKEY   = "\x1b[1;35m";       /* color mode key - bright magenta */
static const char *COL_CNAME  = "\x1b[38;5;69m";    /* color mode name - blue */
static const char *COL_CVALUE = "\x1b[1;36m";       /* color mode value - cyan bold */
static const char *COL_ESEL   = "\x1b[7m";          /* reverse video for selection */
static const char *COL_EKEY   = NULL;
static const char *COL_ENAME  = NULL;
static const char *COL_EVALUE = NULL;
/* syntax colors for editor expression tokens */
#define COL_ENUM    "\x1b[38;5;220m"    /* numbers - yellow */
#define COL_EFUNC   "\x1b[38;5;39m"     /* identifiers/functions - cyan */
#define COL_EOP     "\x1b[1;35m"        /* operators - magenta bold */
#define COL_EPAR    "\x1b[38;5;244m"    /* parentheses/commas - grey */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// utility and terminal helpers moved to util.c and terminal.c

// ----------------------------- config --------------------------------------
typedef enum { MODE_EXPR=0, MODE_MANDELBROT=1, MODE_JULIA=2 } ModeType;

typedef enum { INFO_ALL=0, INFO_NONE=1, INFO_VALUES=2 } InfoMode;

/* application run modes */
typedef enum { RUNMODE_PLAYER=0, RUNMODE_EDITOR=1 } RunMode;

/* editor submodes for expression editing */
typedef enum { ESM_DRAW=0, ESM_COLOR=1 } EditorSubMode;

/* editable parameters in editor mode */
typedef enum {
    EP_FPS=0,
    EP_EXPR=1,
    EP_COUNT
} EditorParam;

/* expression tokenization for editor */
typedef enum {
    TOK_NUMBER,
    TOK_IDENT,
    TOK_OPERATOR,
    TOK_PAREN,
    TOK_OTHER
} TokenType;

typedef struct {
    TokenType type;
    char      text[64];
} Token;

#define MAX_TOKENS 128

/* targets for generic text editing */
typedef enum {
    EDIT_TARGET_EXPR=0,
    EDIT_TARGET_EXPORT=1,
    EDIT_TARGET_IMPORT=2
} EditTarget;

typedef struct {
    // render
    int fps;
    int use_color;
    int color_func;       // 1=use function math for color palette index
    int transparent_ws;   // don't color spaces
    long duration_ms;     // -1 for infinite
    int width, height;

    // charset (fallback if no baked char palette chosen)
    char charset[256];

    // mode
    ModeType mode;

    // expr (value -> char selection)
    char expr_value[1024];

    // fallback color expr (only if no color palette chosen)
    char expr_color[1024];

    // fractal
    int max_iter;
    double cx, cy;
    double scale;
    double j_re, j_im;

    // background fill glyph (UTF-8)
    char background_utf8[8]; // " " (space) means no fill; UTF-8 single-cell recommended
} Config;

static void set_defaults(Config *c){
    memset(c,0,sizeof(*c));
    c->fps = 30;
    c->use_color = 1;
    c->color_func = 0;
    c->transparent_ws = 1;
    c->duration_ms = -1;
    c->width = 0; c->height = 0;
    strcpy(c->charset, " .:-=+*#%@");
    c->mode = MODE_EXPR;
    strcpy(c->expr_value, "sin(6.0*(x+0.2*sin(t*0.7))+t)*cos(6.0*(y+0.2*cos(t*0.5))-t)");
    strcpy(c->expr_color, "128+127*sin(t+3.0*r)");
    c->max_iter = 200;
    c->cx = -0.5; c->cy = 0.0;
    c->scale = 2.8;
    c->j_re = -0.8; c->j_im = 0.156;
    strcpy(c->background_utf8, " "); // default edges-only
}

// --------------- baked presets & palettes (generated headers) ---------------
#ifdef BAKE_PRESETS
#include "baked_presets.h"
#else
static const struct { const char *name; const char *ini; } g_baked_presets[] = {};
static const size_t g_baked_presets_count = 0;
#endif

#ifdef BAKE_PALETTES
#include "baked_palettes.h"  // char: g_char_pals[]; color: g_color_pals[]; counts
#else
static const struct { const char *name; const char *text; } g_char_pals[] = {};
static const size_t g_char_pals_count = 0;
static const struct { const char *name; const char *text; } g_color_pals[] = {};
static const size_t g_color_pals_count = 0;
#endif

static int strieq(const char *a,const char *b){
    for(;*a && *b; ++a,++b){ char ca=*a, cb=*b; if(ca>='A'&&ca<='Z') ca+=32; if(cb>='A'&&cb<='Z') cb+=32; if(ca!=cb) return 0; }
    return *a==0 && *b==0;
}

// ----------------------------- INI helpers ---------------------------------
static void parse_ini(Config *c, const char *text){
    char sect[64]="";
    const char *p=text;
    while(*p){
        const char *line=p;
        const char *nl=strchr(p,'\n');
        size_t len = nl? (size_t)(nl-p) : strlen(p);
        char buf[2048];
        if(len>=sizeof(buf)) len=sizeof(buf)-1;
        memcpy(buf,line,len); buf[len]=0;
        p = nl? nl+1 : p+len;

        char *sc = strpbrk(buf, "#;"); if(sc) *sc=0;
        char *s=buf; while(*s==' '||*s=='\t'||*s=='\r') s++;
        size_t L=strlen(s);
        while(L>0 && (s[L-1]==' '||s[L-1]=='\t'||s[L-1]=='\r')) s[--L]=0;
        if(*s==0) continue;

        if(s[0]=='['){
            char *r=strchr(s,']');
            if(r){ *r=0; strncpy(sect, s+1, sizeof(sect)-1); sect[sizeof(sect)-1]=0; }
            continue;
        }
        char *eq=strchr(s,'='); if(!eq) continue;
        *eq=0; char *key=s; char *val=eq+1;
        while(*val==' '||*val=='\t') val++;
        if(*val=='\"' || *val=='\''){ char q=*val; size_t vlen=strlen(val); if(vlen>=2 && val[vlen-1]==q){ val[vlen-1]=0; val++; } }

        if(strieq(sect,"render")){
            if(strieq(key,"fps")) c->fps = atoi(val);
            else if(strieq(key,"use_color")) c->use_color = atoi(val);
            else if(strieq(key,"color_func")) c->color_func = atoi(val);
            else if(strieq(key,"transparent_ws")||strieq(key,"transparent_spaces")) c->transparent_ws = atoi(val);
            else if(strieq(key,"duration")) c->duration_ms = (long)(atof(val)*1000.0);
            else if(strieq(key,"width")) c->width = atoi(val);
            else if(strieq(key,"height")) c->height = atoi(val);
            else if(strieq(key,"charset")) strncpy(c->charset,val,sizeof(c->charset)-1);
            else if(strieq(key,"background")||strieq(key,"background_char")){
                strncpy(c->background_utf8,val,sizeof(c->background_utf8)-1);
                c->background_utf8[sizeof(c->background_utf8)-1]=0;
            }
        } else if(strieq(sect,"mode")){
            if(strieq(key,"type")){
                if(strieq(val,"expr")) c->mode=MODE_EXPR;
                else if(strieq(val,"mandelbrot")) c->mode=MODE_MANDELBROT;
                else if(strieq(val,"julia")) c->mode=MODE_JULIA;
            }
        } else if(strieq(sect,"expr")){
            if(strieq(key,"value")) strncpy(c->expr_value,val,sizeof(c->expr_value)-1);
            else if(strieq(key,"color")) strncpy(c->expr_color,val,sizeof(c->expr_color)-1);
        } else if(strieq(sect,"fractal")){
            if(strieq(key,"max_iter")) c->max_iter = atoi(val);
            else if(strieq(key,"center_x")) c->cx = atof(val);
            else if(strieq(key,"center_y")) c->cy = atof(val);
            else if(strieq(key,"scale")) c->scale = atof(val);
            else if(strieq(key,"c_re")) c->j_re = atof(val);
            else if(strieq(key,"c_im")) c->j_im = atof(val);
        }
    }
}

// ----------------------------- generic kv extract --------------------------
static int extract_value_any(const char *text, const char *key, char *out, size_t outsz){
    const char *p = text;
    size_t klen=strlen(key);
    while((p=strstr(p,key))){
        const char *q = p + klen;
        while(*q==' '||*q=='\t') q++;
        if(*q!='='){ p++; continue; }
        q++;
        while(*q==' '||*q=='\t') q++;
        if(*q=='\"' || *q=='\''){
            char quote=*q++; const char *end=strchr(q,quote);
            if(!end) return 0;
            size_t L=(size_t)(end-q);
            if(L>=outsz) L=outsz-1;
            memcpy(out,q,L);
            out[L]=0;
            return 1;
        }else{
            const char *end=q;
            while(*end && *end!='\n' && *end!='\r') end++;
            size_t L=(size_t)(end-q);
            while(L && (q[L-1]==' '||q[L-1]=='\t')) L--;
            if(L>=outsz) L=outsz-1;
            memcpy(out,q,L);
            out[L]=0;
            return 1;
        }
    }
    return 0;
}

// ----------------------------- color palette parsing -----------------------
static int parse_color_codes_from_text(const char *text, int *codes, int *count){
    *count=0;
    char buf[512];
    if(extract_value_any(text,"codes",buf,sizeof(buf))){
        char *s=buf;
        while(*s && *count<10){
            while(*s==','||*s==' '||*s=='\t') s++;
            if(!*s) break;
            char *e=s; while(*e && *e!=',' && *e!=' ' && *e!='\t') e++;
            char tmp[32]; size_t L=(size_t)(e-s); if(L>=sizeof(tmp)) L=sizeof(tmp)-1;
            memcpy(tmp,s,L); tmp[L]=0;
            codes[*count] = atoi(tmp);
            (*count)++;
            s=e;
        }
        return (*count)>0;
    }
    int found=0;
    for(int i=0;i<10;i++){
        char key1[32], key2[32];
        snprintf(key1,sizeof(key1),"c%d",i);
        snprintf(key2,sizeof(key2),"color%d",i);
        if(extract_value_any(text,key1,buf,sizeof(buf)) || extract_value_any(text,key2,buf,sizeof(buf))){
            codes[i]=atoi(buf);
            found=1;
            *count=i+1;
        }
    }
    return found && (*count)>0;
}

static int parse_effect_index_expr(const char *text, char *expr, size_t sz){
    if(extract_value_any(text,"index",expr,sz)) return 1;
    if(extract_value_any(text,"index_expr",expr,sz)) return 1;
    if(extract_value_any(text,"expr_index",expr,sz)) return 1;
    return 0;
}

// ----------------------------- expr parser ---------------------------------
typedef struct { const char *s; } Parser;
static void   skip_ws(Parser *p){ while(*p->s==' '||*p->s=='\t') p->s++; }
static int    accept(Parser *p, char c){ skip_ws(p); if(*p->s==c){ p->s++; return 1; } return 0; }

static int match(Parser *p,const char *kw){
    skip_ws(p);
    const char *a=p->s,*b=kw;
    while(*a && *b){
        char ca=*a, cb=*b;
        if(ca>='A'&&ca<='Z') ca+=32;
        if(cb>='A'&&cb<='Z') cb+=32;
        if(ca!=cb) return 0;
        a++; b++;
    }
    if(*b==0){
        char c=*a;
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_') return 0;
        p->s=a; return 1;
    }
    return 0;
}

typedef struct { double x,y,i,j,t,r,a,n; } Vars;
static double parse_expr(Parser *p, const Vars *v);

static double parse_number(Parser *p){
    skip_ws(p);
    char *end; double val = strtod(p->s, &end);
    if(end==p->s) return NAN;
    p->s = end; return val;
}

static double parse_primary(Parser *p, const Vars *vars){
    skip_ws(p);
    if(accept(p,'(')){ double e=parse_expr(p,vars); accept(p,')'); return e; }
    if(match(p,"x")) return vars->x;
    if(match(p,"y")) return vars->y;
    if(match(p,"i")) return vars->i;
    if(match(p,"j")) return vars->j;
    if(match(p,"t")) return vars->t;
    if(match(p,"r")) return vars->r;
    if(match(p,"a")) return vars->a;
    if(match(p,"n")) return vars->n;

    const char *save=p->s; char name[16]={0}; int k=0;
    while((*p->s>='a'&&*p->s<='z')||(*p->s>='A'&&*p->s<='Z')){ if(k<15) name[k++]=*p->s; p->s++; }
    name[k]=0;
    if(k>0){
        for(int q=0;q<k;q++) if(name[q]>='A'&&name[q]<='Z') name[q]+=32;
        if(accept(p,'(')){
            double a = parse_expr(p,vars), b=0;
            if(accept(p,',')) b=parse_expr(p,vars);
            accept(p,')');
            if(strcmp(name,"sin")==0) return sin(a);
            if(strcmp(name,"cos")==0) return cos(a);
            if(strcmp(name,"tan")==0) return tan(a);
            if(strcmp(name,"asin")==0) return asin(a);
            if(strcmp(name,"acos")==0) return acos(a);
            if(strcmp(name,"atan")==0) return atan(a);
            if(strcmp(name,"exp")==0) return exp(a);
            if(strcmp(name,"log")==0) return log(fabs(a)<1e-300?1e-300:a);
            if(strcmp(name,"sqrt")==0) return sqrt(fabs(a));
            if(strcmp(name,"abs")==0) return fabs(a);
            if(strcmp(name,"floor")==0) return floor(a);
            if(strcmp(name,"ceil")==0) return ceil(a);
            if(strcmp(name,"min")==0) return (a<b)?a:b;
            if(strcmp(name,"max")==0) return (a>b)?a:b;
            if(strcmp(name,"pow")==0) return pow(a,b);
            if(strcmp(name,"mod")==0) return fmod(a,b==0?1:b);
            return NAN;
        } else {
            p->s = save;
        }
    }
    double n = parse_number(p);
    if(isnan(n)) return 0.0;
    return n;
}
static double parse_unary(Parser *p,const Vars *v){ if(accept(p,'+')) return parse_unary(p,v); if(accept(p,'-')) return -parse_unary(p,v); return parse_primary(p,v); }
static double parse_power(Parser *p,const Vars *v){ double a=parse_unary(p,v); while(accept(p,'^')){ double b=parse_unary(p,v); a=pow(a,b);} return a; }
static int    match_word(Parser *p,const char *w){ const char *s=p->s; if(match(p,w)) return 1; p->s=s; return 0; }
static double parse_term(Parser *p,const Vars *v){
    double a=parse_power(p,v);
    for(;;){
        if(accept(p,'*')) a*=parse_power(p,v);
        else if(accept(p,'/')){ double b=parse_power(p,v); a/=(fabs(b)<1e-300?1e-300:b); }
        else if(match_word(p,"mod")){ double b=parse_power(p,v); a=fmod(a,(fabs(b)<1e-300?1e-300:b)); }
        else break;
    }
    return a;
}
static double parse_expr(Parser *p,const Vars *v){ double a=parse_term(p,v); for(;;){ if(accept(p,'+')) a+=parse_term(p,v); else if(accept(p,'-')) a-=parse_term(p,v); else break; } return a; }
static double eval_expr(const char *src, const Vars *v){ Parser p={.s=src}; double out=parse_expr(&p,v); if(isnan(out)||!isfinite(out)) return 0.0; return out; }

// ----------------------------- UTF-8 helpers -------------------------------
static int utf8_len(unsigned char c){
    if(c<0x80) return 1;
    if((c>>5)==0x6) return 2;
    if((c>>4)==0xE) return 3;
    if((c>>3)==0x1E) return 4;
    return 1;
}

typedef struct {
    char  glyph[8];   // room for up to 4-byte UTF-8 + NUL
    unsigned char glen;
    int is_space;     // true ASCII space
} Glyph;

typedef struct {
    Glyph g[256];
    int count;
    char name[64];
} ActiveCharset;

// split continuous string " ▁▂▃…" into glyphs
static void cs_from_string(ActiveCharset *cs, const char *s, const char *name){
    memset(cs,0,sizeof(*cs));
    if(name) snprintf(cs->name,sizeof(cs->name),"%s",name);
    const unsigned char *p=(const unsigned char*)s;
    while(*p && cs->count<256){
        int L=utf8_len(*p);
        if(L<1) L=1;
        Glyph *g=&cs->g[cs->count++];
        int cpy = (L>= (int)sizeof(g->glyph)-1) ? (int)sizeof(g->glyph)-1 : L;
        memcpy(g->glyph,p,cpy);
        g->glyph[cpy]=0;
        g->glen=(unsigned char)cpy;
        g->is_space = (cpy==1 && g->glyph[0]==' ');
        p+=L;
    }
    if(cs->count==0){
        cs->g[0].glyph[0]=' '; cs->g[0].glyph[1]=0; cs->g[0].glen=1; cs->g[0].is_space=1; cs->count=1;
    }
}

// split CSV: glyphs=" ,·,•,░,▒,▓,@,#"
static void cs_from_csv(ActiveCharset *cs, const char *csv, const char *name){
    memset(cs,0,sizeof(*cs));
    if(name) snprintf(cs->name,sizeof(cs->name),"%s",name);
    const char *s=csv;
    while(*s && cs->count<256){
        while(*s==' '||*s=='\t'||*s==',') s++;
        if(!*s) break;
        const char *e=s;
        while(*e && *e!=',') e++;
        size_t L=(size_t)(e-s);
        while(L && (s[L-1]==' '||s[L-1]=='\t')) L--;
        Glyph *g=&cs->g[cs->count++];
        size_t cpy = L>=sizeof(g->glyph)-1 ? sizeof(g->glyph)-1 : L;
        memcpy(g->glyph,s,cpy);
        g->glyph[cpy]=0;
        g->glen=(unsigned char)cpy;
        g->is_space = (cpy==1 && g->glyph[0]==' ');
        s = (*e==',') ? e+1 : e;
    }
    if(cs->count==0){
        cs->g[0].glyph[0]=' '; cs->g[0].glyph[1]=0; cs->g[0].glen=1; cs->g[0].is_space=1; cs->count=1;
    }
}

static void emit_glyph(const Glyph *g){ ssize_t w=write(STDOUT_FILENO,g->glyph,g->glen); (void)w; }

// ----------------------------- baked palettes hooks ------------------------
static int g_charpal_idx = -1;        // -1 => fallback from config string
static int g_charpal_fb_idx = 0;

static int g_colorpal_idx = -1;       // -1 => legacy color expr

// extract char palette content
static int extract_value_any(const char*, const char*, char*, size_t); // fwd (already above)
static void parse_char_palette_text(const char *text, ActiveCharset *cs){
    char name[64]={0}, glyphs[1024]={0}, charset[1024]={0};
    extract_value_any(text,"name",name,sizeof(name));
    if(extract_value_any(text,"glyphs",glyphs,sizeof(glyphs))){
        cs_from_csv(cs,glyphs,name[0]?name:NULL);
    }else if(extract_value_any(text,"charset",charset,sizeof(charset))){
        cs_from_string(cs,charset,name[0]?name:NULL);
    }else{
        cs_from_string(cs," .:-=+*#%@", "fallback");
    }
}

// color palette
typedef struct {
    int codes[10];
    int count;
    char index_expr[256];
    char name[64];
    int valid;
} ActiveColor;

static int parse_color_codes_from_text(const char*, int*, int*); // fwd
static int parse_effect_index_expr(const char*, char*, size_t);  // fwd

static void colorpal_parse_from_text(const char *name, const char *text, ActiveColor *ac){
    memset(ac,0,sizeof(*ac));
    int ok_codes = parse_color_codes_from_text(text, ac->codes, &ac->count);
    char expr[256]; expr[0]=0;
    int ok_effect = parse_effect_index_expr(text, expr, sizeof(expr));
    if(ok_codes){
        snprintf(ac->index_expr,sizeof(ac->index_expr),"%s", ok_effect?expr:"0");
        if(name) snprintf(ac->name,sizeof(ac->name),"%s",name);
        ac->valid = 1;
    }
}

static void colorpal_from_selection(ActiveColor *ac){
    if(g_colorpal_idx >= 0 && (size_t)g_colorpal_idx < g_color_pals_count){
        colorpal_parse_from_text(g_color_pals[g_colorpal_idx].name, g_color_pals[g_colorpal_idx].text, ac);
    }else{
        memset(ac,0,sizeof(*ac));
    }
}

// ----------------------------- background glyph ----------------------------
static const char *BG_CANDIDATES[] = { " ", ".", "·", "•", ":", "°", "░", "▒", "▓", "@", "#"};
static const int BG_CAND_COUNT = (int)(sizeof(BG_CANDIDATES)/sizeof(BG_CANDIDATES[0]));

static int utf8_eq(const char *a,const char *b){ return strcmp(a,b)==0; }

typedef struct {
    Glyph bg;
    int   idx_in_cycle;  // -1 if custom (not in cycle list)
} BackgroundState;

static void glyph_from_utf8(Glyph *g, const char *s){
    size_t L=strlen(s);
    if(L>=sizeof(g->glyph)) L=sizeof(g->glyph)-1;
    memcpy(g->glyph, s, L);
    g->glyph[L]=0;
    g->glen=(unsigned char)L;
    g->is_space = (L==1 && g->glyph[0]==' ');
}

static void bg_from_config(BackgroundState *bgs, const char *utf8){
    // set bg glyph
    glyph_from_utf8(&bgs->bg, utf8?utf8:" ");
    // find in cycle list
    bgs->idx_in_cycle = -1;
    for(int i=0;i<BG_CAND_COUNT;i++){
        if(utf8_eq(utf8, BG_CANDIDATES[i])){ bgs->idx_in_cycle = i; break; }
    }
}

static void bg_cycle_next(BackgroundState *bgs){
    int idx = bgs->idx_in_cycle;
    if(idx<0) idx = 0; // start cycle from first candidate
    else idx = (idx+1) % BG_CAND_COUNT;
    bgs->idx_in_cycle = idx;
    glyph_from_utf8(&bgs->bg, BG_CANDIDATES[idx]);
}

// ----------------------------- rendering -----------------------------------
static const char *FALLBACK_CHARSETS[] = {
    " .:-=+*#%@",
    " .'`^\",:;Il!i><~+_-?][}{1)(|\\/*tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$",
    " ░▒▓█",
    " ▁▂▃▄▅▆▇█",
};

typedef struct {
    Config        cfg;
    int           tw, th;
    double        t0;
    int           paused;
    InfoMode      info_mode;     // info bar mode
    int           info_rows;     // computed lines reserved for info bar

    RunMode       run_mode;      // player or editor
    EditorParam   editor_param;  // currently selected parameter
    int           editor_step_idx; // index into step table
    EditorSubMode editor_submode; // draw or color expression
    int           live_preview;   // live preview toggle
    int           editing_text;  // editing raw text flag
    char          edit_buf[1024];
    char          edit_orig[1024];
    size_t        edit_len;
    size_t        edit_cursor;
    EditTarget    edit_target;
    int           editing_tokens; // editing expression tokens
    Token         expr_tokens[MAX_TOKENS];
    int           expr_tok_count;
    int           expr_tok_sel;
    int           edit_tok_idx;   // token index when raw-editing a block
    int           pending_brace;  // awaiting brace type after ^P

    ActiveCharset acs;
    ActiveColor   cur_col;
    int           cached_col_idx;
    int           cur_preset_idx;

    BackgroundState bg;
} App;

static void app_pick_charset(App *a){
    if(g_charpal_idx >= 0 && (size_t)g_charpal_idx < g_char_pals_count){
        parse_char_palette_text(g_char_pals[g_charpal_idx].text,&a->acs);
        if(!a->acs.name[0]) snprintf(a->acs.name,sizeof(a->acs.name),"%s", g_char_pals[g_charpal_idx].name);
    }else{
        if(a->cfg.charset[0]) cs_from_string(&a->acs, a->cfg.charset, "cfg");
        else cs_from_string(&a->acs, FALLBACK_CHARSETS[g_charpal_fb_idx % 4], "fallback");
    }
}

static void app_init_background(App *a){
    bg_from_config(&a->bg, a->cfg.background_utf8);
}

static void app_query_size(App *a){
    int w,h; get_tty_size(&w,&h);
    if(a->cfg.width>0) w = a->cfg.width;
    if(a->cfg.height>0) h = a->cfg.height;
    a->tw=w; a->th=h;
}

static void append_str(const char *s){ ssize_t w=write(STDOUT_FILENO,s,strlen(s)); (void)w; }

/* --- editor helpers ----------------------------------------------------- */
static const double EDIT_STEPS[] = {0.01,0.1,1.0,10.0};
static const int EDIT_STEP_COUNT = sizeof(EDIT_STEPS)/sizeof(EDIT_STEPS[0]);

static void editor_set_submode(App *a, EditorSubMode m){
    a->editor_submode = m;
    a->editing_text = 0;
    a->editing_tokens = 0;
    COL_EKEY   = (m==ESM_DRAW)?COL_DKEY:COL_CKEY;
    COL_ENAME  = (m==ESM_DRAW)?COL_DNAME:COL_CNAME;
    COL_EVALUE = (m==ESM_DRAW)?COL_DVALUE:COL_CVALUE;
}

static void editor_adjust_param(App *a, int dir){
    double step = EDIT_STEPS[a->editor_step_idx];
    switch(a->editor_param){
        case EP_FPS:
            a->cfg.fps = (int)clamp_long(a->cfg.fps + dir*(int)step,1,240);
            break;
        default:
            break;
    }
}

/* token helpers ----------------------------------------------------------- */
static int tokenize_expr(const char *expr, Token *toks, int max){
    int n=0; const char *p=expr;
    while(*p && n<max){
        if(isdigit((unsigned char)*p) || (*p=='.' && isdigit((unsigned char)p[1]))){
            int len=0; while((isdigit((unsigned char)*p) || *p=='.') && len<63){ toks[n].text[len++]=*p++; }
            toks[n].text[len]=0; toks[n].type=TOK_NUMBER; n++;
        }else if(isalpha((unsigned char)*p)){
            int len=0; while((isalnum((unsigned char)*p) || *p=='_') && len<63){ toks[n].text[len++]=*p++; }
            toks[n].text[len]=0; toks[n].type=TOK_IDENT; n++;
        }else{
            char ch=*p++;
            toks[n].text[0]=ch; toks[n].text[1]=0;
            if(strchr("+-*/%^",ch)) toks[n].type=TOK_OPERATOR;
            else if(ch=='('||ch==')'||ch=='['||ch==']'||ch=='{'||ch=='}'||ch==',') toks[n].type=TOK_PAREN;
            else toks[n].type=TOK_OTHER;
            n++;
        }
    }
    return n;
}

static const char *token_color(TokenType t){
    switch(t){
        case TOK_NUMBER: return COL_ENUM;
        case TOK_IDENT:  return COL_EFUNC;
        case TOK_OPERATOR: return COL_EOP;
        case TOK_PAREN: return COL_EPAR;
        default: return COL_EVALUE;
    }
}

static TokenType classify_token_text(const char *s){
    if(!s || !*s) return TOK_OTHER;
    int len=strlen(s);
    int isnum=1, isident=1;
    if(!(isdigit((unsigned char)s[0]) || s[0]=='.')) isnum=0;
    if(!(isalpha((unsigned char)s[0]) || s[0]=='_')) isident=0;
    for(int i=1;i<len;i++){
        if(!(isdigit((unsigned char)s[i]) || s[i]=='.')) isnum=0;
        if(!(isalnum((unsigned char)s[i]) || s[i]=='_')) isident=0;
    }
    if(isnum) return TOK_NUMBER;
    if(isident) return TOK_IDENT;
    if(len==1){
        char ch=s[0];
        if(strchr("+-*/%^",ch)) return TOK_OPERATOR;
        if(ch=='('||ch==')'||ch=='['||ch==']'||ch=='{'||ch=='}'||ch==',') return TOK_PAREN;
    }
    return TOK_OTHER;
}

static int is_open_brace(char c){ return c=='('||c=='['||c=='{'; }
static int is_close_brace(char c){ return c==')'||c==']'||c=='}'; }
static char matching_close(char open){ if(open=='(') return ')'; if(open=='[') return ']'; if(open=='{') return '}'; return 0; }

static char *current_expr(App *a){
    return (a->editor_submode==ESM_DRAW)?a->cfg.expr_value:a->cfg.expr_color;
}

static void validate_expr_string(char *expr){
    if(!expr) return;
    char out[1024]; size_t pos=0; char stack[128]; int sp=0;
    for(const char *p=expr; *p && pos<sizeof(out)-1; ++p){
        char ch=*p;
        if(is_open_brace(ch)){
            if(sp < (int)sizeof(stack)) stack[sp++]=ch;
            out[pos++]=ch;
        }else if(is_close_brace(ch)){
            if(sp>0){
                char want=matching_close(stack[sp-1]);
                if(ch!=want) ch=want;
                sp--; out[pos++]=ch;
            }
        }else{
            out[pos++]=ch;
        }
    }
    while(sp>0 && pos<sizeof(out)-1){ out[pos++]=matching_close(stack[--sp]); }
    out[pos]=0;
    Vars v={0};
    double r=eval_expr(out,&v);
    if(!isfinite(r)) snprintf(out,sizeof(out),"0");
    strncpy(expr,out,1023); expr[1023]=0;
}

static void editor_tokens_to_expr(App *a){
    char buf[1024]; size_t pos=0;
    for(int i=0;i<a->expr_tok_count;i++){
        size_t len=strlen(a->expr_tokens[i].text);
        if(pos+len >= sizeof(buf)) break;
        memcpy(buf+pos,a->expr_tokens[i].text,len); pos+=len;
    }
    buf[pos]=0;
    char *expr=current_expr(a);
    strncpy(expr, buf, 1023);
    expr[1023]=0;
}

static void format_expr_colored(const char *expr, char *out, size_t outsz){
    Token toks[MAX_TOKENS];
    int cnt = tokenize_expr(expr,toks,MAX_TOKENS);
    size_t pos=0;
    for(int i=0;i<cnt && pos<outsz; i++){
        const char *col = token_color(toks[i].type);
        pos += snprintf(out+pos,outsz-pos,"%s%s%s",col,toks[i].text,COL_RESET);
    }
    if(pos<outsz) out[pos]=0; else out[outsz-1]=0;
}

static void format_tokens_line(App *a, char *out, size_t outsz){
    size_t pos=0;
    for(int i=0;i<a->expr_tok_count && pos<outsz; i++){
        Token *t=&a->expr_tokens[i];
        const char *col=token_color(t->type);
        const char *sel=(i==a->expr_tok_sel)?COL_ESEL:"";
        pos += snprintf(out+pos,outsz-pos,"%s[%s%s%s]%s",COL_RESET,sel,col,t->text,COL_RESET);
        if(i+1<a->expr_tok_count) pos+=snprintf(out+pos,outsz-pos," ");
    }
    if(pos<outsz) out[pos]=0; else out[outsz-1]=0;
}

static void editor_enter_token_mode(App *a){
    a->expr_tok_count = tokenize_expr(current_expr(a), a->expr_tokens, MAX_TOKENS);
    a->expr_tok_sel = 0;
    a->editing_tokens = 1;
    a->edit_tok_idx = -1;
    a->pending_brace = 0;
}

static void editor_exit_token_mode(App *a){
    editor_tokens_to_expr(a);
    a->editing_tokens = 0;
    a->edit_tok_idx = -1;
    a->pending_brace = 0;
}

static void editor_adjust_token(App *a, int dir){
    if(a->expr_tok_sel<0 || a->expr_tok_sel>=a->expr_tok_count) return;
    Token *t=&a->expr_tokens[a->expr_tok_sel];
    double step = EDIT_STEPS[a->editor_step_idx];
    if(t->type==TOK_NUMBER){
        double v=atof(t->text);
        v += dir*step;
        snprintf(t->text,sizeof(t->text),"%g",v);
    }else if(t->type==TOK_OPERATOR){
        const char ops[]="+-*/%^";
        char *p=strchr(ops,t->text[0]);
        if(p){ int idx=(int)(p-ops); int len=strlen(ops); idx=(idx+dir+len)%len; t->text[0]=ops[idx]; t->text[1]=0; }
    }else if(t->type==TOK_IDENT){
        const char *funcs[]={"sin","cos","tan"};
        int n=3; int idx=-1; for(int i=0;i<n;i++) if(strcmp(t->text,funcs[i])==0){ idx=i; break; }
        if(idx>=0){ idx=(idx+dir+n)%n; snprintf(t->text,sizeof(t->text),"%s",funcs[idx]); }
    }
}

static void insert_brace_block(App *a, char open){
    char close=matching_close(open); if(!close) close=')', open='(';
    if(a->expr_tok_count+3 >= MAX_TOKENS) return;
    int idx=a->expr_tok_sel+1;
    if(idx<0) idx=0;
    if(idx>a->expr_tok_count) idx=a->expr_tok_count;
    memmove(&a->expr_tokens[idx+3], &a->expr_tokens[idx], (a->expr_tok_count - idx)*sizeof(Token));
    a->expr_tokens[idx].text[0]=open; a->expr_tokens[idx].text[1]=0; a->expr_tokens[idx].type=TOK_PAREN;
    a->expr_tokens[idx+1].text[0]=0; a->expr_tokens[idx+1].type=TOK_OTHER;
    a->expr_tokens[idx+2].text[0]=close; a->expr_tokens[idx+2].text[1]=0; a->expr_tokens[idx+2].type=TOK_PAREN;
    a->expr_tok_count+=3;
    a->expr_tok_sel=idx+1;
    if(a->live_preview) editor_tokens_to_expr(a);
}

static void remove_brace_block(App *a){
    int idx=a->expr_tok_sel;
    if(a->expr_tok_count<=0) return;
    int start=-1, end=-1; char open=0; char close=0; int depth=0;
    for(int i=idx;i>=0;i--){
        char ch=a->expr_tokens[i].text[0];
        if(is_close_brace(ch)) depth++;
        else if(is_open_brace(ch)){
            if(depth==0){ start=i; open=ch; break; }
            depth--;
        }
    }
    if(start<0) return;
    close=matching_close(open);
    depth=0;
    for(int i=start;i<a->expr_tok_count;i++){
        char ch=a->expr_tokens[i].text[0];
        if(ch==open) depth++;
        else if(ch==close){
            depth--;
            if(depth==0){ end=i; break; }
        }
    }
    if(end<0) return;
    int remove_count=end-start+1;
    memmove(&a->expr_tokens[start], &a->expr_tokens[end+1], (a->expr_tok_count-end-1)*sizeof(Token));
    a->expr_tok_count -= remove_count;
    if(a->expr_tok_count<=0){ a->expr_tok_sel=0; }
    else if(a->expr_tok_sel>=start){
        a->expr_tok_sel = (start < a->expr_tok_count)?start:a->expr_tok_count-1;
    }
    if(a->live_preview) editor_tokens_to_expr(a);
}

static void start_text_edit(App *a, int tok_idx){
    if(a->editor_param==EP_EXPR){
        a->editing_text = 1;
        a->edit_target = EDIT_TARGET_EXPR;
        a->edit_tok_idx = tok_idx;
        if(tok_idx>=0){
            a->editing_tokens = 1;
            a->pending_brace = 0;
            Token *t=&a->expr_tokens[tok_idx];
            strncpy(a->edit_buf, t->text, sizeof(a->edit_buf)-1);
        }else{
            a->editing_tokens = 0;
            char *expr = current_expr(a);
            strncpy(a->edit_buf, expr, sizeof(a->edit_buf)-1);
        }
        a->edit_buf[sizeof(a->edit_buf)-1]=0;
        strncpy(a->edit_orig, a->edit_buf, sizeof(a->edit_orig)-1);
        a->edit_orig[sizeof(a->edit_orig)-1]=0;
        a->edit_len = strlen(a->edit_buf);
        a->edit_cursor = a->edit_len;
    }
}

static void start_path_edit(App *a, int import_mode){
    a->editing_text = 1;
    a->edit_target = import_mode ? EDIT_TARGET_IMPORT : EDIT_TARGET_EXPORT;
    a->editing_tokens = 0;
    a->edit_tok_idx = -1;
    a->pending_brace = 0;
    a->edit_buf[0]=0; a->edit_orig[0]=0;
    a->edit_len=0; a->edit_cursor=0;
}

static int load_config_from_file(App *a, const char *path);
static int save_function_to_file(const Config *c, const char *path);

static void apply_edit_text(App *a, int exit_after){
    if(a->edit_target==EDIT_TARGET_EXPR){
        if(a->editor_param==EP_EXPR){
            if(a->edit_tok_idx>=0){
                Token *t=&a->expr_tokens[a->edit_tok_idx];
                strncpy(t->text, a->edit_buf, sizeof(t->text)-1);
                t->text[sizeof(t->text)-1]=0;
                t->type=classify_token_text(t->text);
                if(a->live_preview) editor_tokens_to_expr(a);
            }else{
                char *expr = current_expr(a);
                strncpy(expr, a->edit_buf, 1023);
                expr[1023]=0;
            }
        }
    }else if(a->edit_target==EDIT_TARGET_EXPORT){
        if(exit_after) save_function_to_file(&a->cfg, a->edit_buf);
    }else if(a->edit_target==EDIT_TARGET_IMPORT){
        if(exit_after) load_config_from_file(a, a->edit_buf);
    }
    if(exit_after){ a->editing_text = 0; a->edit_tok_idx=-1; a->pending_brace=0; a->edit_target=EDIT_TARGET_EXPR; }
}

static void cancel_edit_text(App *a){
    if(a->edit_target==EDIT_TARGET_EXPR){
        if(a->editor_param==EP_EXPR){
            if(a->edit_tok_idx>=0){
                Token *t=&a->expr_tokens[a->edit_tok_idx];
                strncpy(t->text, a->edit_orig, sizeof(t->text)-1);
                t->text[sizeof(t->text)-1]=0;
                t->type=classify_token_text(t->text);
                if(a->live_preview) editor_tokens_to_expr(a);
            }else{
                char *expr = current_expr(a);
                strncpy(expr, a->edit_orig, 1023);
                expr[1023]=0;
            }
        }
    }
    a->editing_text = 0; a->edit_tok_idx=-1; a->pending_brace=0; a->edit_target=EDIT_TARGET_EXPR;
}

static void format_info_strings(App *a, char *line1, size_t n1, char *line2, size_t n2){
    if(a->run_mode==RUNMODE_PLAYER){
        const char *m = (a->cfg.mode==MODE_EXPR)?"expr":(a->cfg.mode==MODE_MANDELBROT)?"mandelbrot":"julia";
        const char *colname = a->cur_col.valid ? a->cur_col.name : "expr";
        char bgdisp[16];
        snprintf(bgdisp,sizeof(bgdisp),"%s", a->bg.bg.glyph[0] ? a->bg.bg.glyph : " ");
        char bgshow[24];
        snprintf(bgshow,sizeof(bgshow),"'%s'", bgdisp);

        if(line1 && n1){
            snprintf(line1,n1,
                COL_RESET "[%sFPS%s:%s%d%s] [%s%s%s](%s%s%s) [%s%s%s](%s%s%s:%s%s%s) [%s%s%s](%s%s%s) [%s%s%s](%s%s%s) [%s%s%s](%sws%s:%s%s%s)" COL_RESET,
                COL_NAME, COL_RESET, COL_VALUE, a->cfg.fps, COL_RESET,
                COL_KEY, "m", COL_RESET, COL_NAME, m, COL_RESET,
                COL_KEY, "c", COL_RESET, COL_NAME, colname, COL_RESET, COL_STATE, a->cfg.color_func?"func":"pal", COL_RESET,
                COL_KEY, "n", COL_RESET, COL_NAME, a->acs.name[0]?a->acs.name:"(unnamed)", COL_RESET,
                COL_KEY, "w", COL_RESET, COL_VALUE, bgshow, COL_RESET,
                COL_KEY, "W", COL_RESET, COL_NAME, COL_RESET, COL_STATE, a->cfg.transparent_ws?"transp":"color", COL_RESET);
        }
        if(line2 && n2){
            snprintf(line2,n2,
                COL_RESET "%s[q]%s quit | %s[p]%s pause | %s[i]%s info | %s[w]%s cycle-bg | %s[W]%s ws-transp | %s[+/-]%s fps | %s[C]%s toggle-color | %s[c]%s next-col | %s[f]%s col-math | %s[n]%s next-char | %s[m]%s next-func | %s[r]%s reload | %s[arrows/[]]%s pan/zoom" COL_RESET,
                COL_KEY, COL_RESET,  /* q */
                COL_KEY, COL_RESET,  /* p */
                COL_KEY, COL_RESET,  /* i */
                COL_KEY, COL_RESET,  /* w */
                COL_KEY, COL_RESET,  /* W */
                COL_KEY, COL_RESET,  /* +/- */
                COL_KEY, COL_RESET,  /* C */
                COL_KEY, COL_RESET,  /* c */
                COL_KEY, COL_RESET,  /* f */
                COL_KEY, COL_RESET,  /* n */
                COL_KEY, COL_RESET,  /* m */
                COL_KEY, COL_RESET,  /* r */
                COL_KEY, COL_RESET); /* arrows */
        }
    }else{ /* editor mode */
        double step = EDIT_STEPS[a->editor_step_idx];
        if(a->editing_tokens){
            if(line1 && n1){
                char buf[4096];
                format_tokens_line(a, buf, sizeof(buf));
                const char *sub = (a->editor_submode==ESM_DRAW)?"DRAW":"COLOR";
                snprintf(line1,n1, "%s[%s%s%s] %s", COL_RESET, COL_EVALUE, sub, COL_RESET, buf);
            }
            if(line2 && n2){
                if(a->editing_text){
                    char buf[4096];
                    format_expr_colored(a->edit_buf, buf, sizeof(buf));
                    snprintf(line2,n2,
                        COL_RESET "Block: %s%s%s (%s^Y%s/%sEnter%s save %s^R%s run %s^X%s cancel %s^L%s live)" COL_RESET,
                        COL_EVALUE, buf, COL_RESET,
                        COL_EKEY, COL_RESET, COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET, COL_EKEY, COL_RESET, COL_EKEY, COL_RESET);
                }else{
                    snprintf(line2,n2,
                        COL_RESET "%s[Enter]%s done | %s[arrows]%s sel/adj | %s[e]%s edit | %s[^O]%s rmblk | %s[^P]%s blk | %s[^E]%s raw | %s[^T]%s player | %s[^L]%s live | %s[Alt+V]%s val | %s[Alt+S]%s save | %s[Alt+I]%s load | %s[i]%s info" COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET);
                }
            }
        }else{
            if(line1 && n1){
                const char *sel1 = (a->editor_param==EP_FPS)?COL_ESEL:COL_RESET;
                const char *sel2 = (a->editor_param==EP_EXPR)?COL_ESEL:COL_RESET;
                char expr_col[4096];
                format_expr_colored(current_expr(a), expr_col, sizeof(expr_col));
                const char *sub = (a->editor_submode==ESM_DRAW)?"DRAW":"COLOR";
                snprintf(line1,n1,
                    "%s[%s%s%s] %s[%sFPS%s:%d]%s %s[%sExpr%s:%s]%s [%sstep%s:%s%.2f%s]",
                    COL_RESET, COL_EVALUE, sub, COL_RESET,
                    sel1, COL_ENAME, COL_EVALUE, a->cfg.fps, COL_RESET,
                    sel2, COL_ENAME, COL_RESET, expr_col, COL_RESET,
                    COL_ENAME, COL_RESET, COL_EVALUE, step, COL_RESET);
            }
            if(line2 && n2){
                if(a->editing_text){
                    if(a->edit_target==EDIT_TARGET_EXPR){
                        char buf[4096];
                        format_expr_colored(a->edit_buf, buf, sizeof(buf));
                        snprintf(line2,n2,
                            COL_RESET "Edit: %s%s%s (%s^Y%s/%sEnter%s save %s^R%s run %s^X%s cancel %s^L%s live)" COL_RESET,
                            COL_EVALUE, buf, COL_RESET,
                            COL_EKEY, COL_RESET, COL_EKEY, COL_RESET,
                            COL_EKEY, COL_RESET, COL_EKEY, COL_RESET, COL_EKEY, COL_RESET);
                    }else{
                        snprintf(line2,n2,
                            COL_RESET "Path: %s%s%s (%s^Y%s/%sEnter%s ok %s^X%s cancel)" COL_RESET,
                            COL_EVALUE, a->edit_buf, COL_RESET,
                            COL_EKEY, COL_RESET, COL_EKEY, COL_RESET, COL_EKEY, COL_RESET);
                    }
                }else{
                    snprintf(line2,n2,
                        COL_RESET "%s[^T]%s player | %s[arrows]%s select/adjust | %s[+/-]%s adjust | %s[[]]%s step | %s[^E]%s edit | %s[^L]%s live | %s[Alt+V]%s val | %s[Alt+S]%s save | %s[Alt+I]%s load | %s[i]%s info" COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET,
                        COL_EKEY, COL_RESET);
                }
            }
        }
    }
}

static int count_wrapped(const char *line, int width){
    if(width<=0 || !line || !*line) return 0;
    int col=0, rows=1; const char *p=line;
    while(*p){
        if(*p=='\x1b'){
            const char *q=strchr(p,'m'); if(!q) break; p=q+1; continue;
        }
        if(col>=width){ rows++; col=0; }
        col++; p++;
    }
    return rows;
}

static int print_wrapped(const char *line, int width, int row_start){
    int col=0; int row=row_start; const char *p=line;
    term_move(row,1);
    while(*p){
        if(*p=='\x1b'){
            const char *q=strchr(p,'m'); if(!q) break; ssize_t w1=write(STDOUT_FILENO,p,q-p+1); (void)w1; p=q+1; continue;
        }
        if(col>=width){ col=0; row++; term_move(row,1); }
        ssize_t w2=write(STDOUT_FILENO,p,1); (void)w2; p++; col++;
    }
    return row - row_start + 1;
}

static void update_info_rows(App *a){
    if(a->info_mode==INFO_NONE){ a->info_rows=0; return; }
    char l1[4096]; char l2[4096];
    format_info_strings(a,l1,sizeof(l1),l2,sizeof(l2));
    int lines = count_wrapped(l1,a->tw);
    if(a->info_mode==INFO_ALL) lines += count_wrapped(l2,a->tw);
    a->info_rows = lines;
}

static void draw_info_bar(App *a){
    static int prev_lines=0;
    int max_lines = (a->info_rows>prev_lines)?a->info_rows:prev_lines;
    if(max_lines){
        int clear_start = a->th - max_lines + 1;
        for(int r=clear_start; r<=a->th; ++r){ term_move(r,1); term_clear_line(); }
    }
    if(a->info_mode==INFO_NONE){ prev_lines=0; return; }
    char line1[4096]; char line2[4096];
    format_info_strings(a,line1,sizeof(line1),line2,sizeof(line2));
    int start = a->th - a->info_rows + 1;
    int l1 = print_wrapped(line1,a->tw,start);
    if(a->info_mode==INFO_ALL){
        print_wrapped(line2,a->tw,start + l1);
    }
    prev_lines = a->info_rows;
}

static inline size_t cs_idx_from_value(const ActiveCharset *cs, double v){
    int n=cs->count; if(n<=1) return 0;
    double t = (v+1.0)*0.5; if(t<0)t=0; if(t>1)t=1;
    size_t idx=(size_t)floor(t*(n-1)+0.5);
    return idx;
}

static inline int col_idx_from_value(const ActiveColor *ac, double v){
    int n = ac->count;
    if(n<=1) return 0;
    double t = (v+1.0)*0.5;
    if(t<0) t=0;
    if(t>1) t=1;
    int idx = (int)floor(t*(n-1)+0.5);
    if(idx<0) idx=0;
    if(idx>=n) idx=n-1;
    return idx;
}

static int pixel_color_code(App *a, int i,int j,double x,double y,double t){
    if(!a->cfg.use_color) return -1;
    if(a->cur_col.valid && a->cur_col.count>0){
        Vars v = { .x=x,.y=y,.i=(double)i,.j=(double)j,.t=t,.r=hypot(x,y),.a=atan2(y,x),.n=(double)a->cur_col.count };
        double idxf = eval_expr(a->cur_col.index_expr, &v);
        long idx = (long)floor(idxf);
        int n = a->cur_col.count;
        if(n<=0) return -1;
        int m = (int)((idx % n + n) % n);
        return a->cur_col.codes[m];
    }else{
        Vars v = { .x=x,.y=y,.i=(double)i,.j=(double)j,.t=t,.r=hypot(x,y),.a=atan2(y,x),.n=0 };
        int ci = (int)lrint(clamp(eval_expr(a->cfg.expr_color,&v),0.0,255.0));
        return ci;
    }
}

// ---- renderers (expr/mandelbrot/julia) with background substitution -------
static void render_expr(App *a, double t){
    const int w=a->tw;
    const int content_h = a->th - a->info_rows;
    double aspect = (double)w/(double)(content_h>0?content_h:1);

    for(int j=0;j<content_h;j++){
        term_move(j+1, 1);
        int color_on=0, last_ci=-2;

        for(int i=0;i<w;i++){
            double x = ( (double)i/(w-1)*2.0 - 1.0 ) * aspect;
            double y = ( (double)j/((content_h-1>0)?(content_h-1):1)*2.0 - 1.0 );
            Vars v = { .x=x,.y=y,.i=(double)i,.j=(double)j,.t=t,.r=hypot(x,y),.a=atan2(y,x),.n=0 };
            double val = eval_expr(a->cfg.expr_value,&v);
            if(val<-1) val=-1; else if(val>1) val=1;
            size_t idx = cs_idx_from_value(&a->acs,val);
            const Glyph *g = &a->acs.g[idx];
            // substitute background when palette gives space
            const Glyph *eg = g->is_space ? &a->bg.bg : g;

            int ci;
            if(a->cfg.color_func && a->cur_col.valid && a->cur_col.count>0){
                int n=a->cur_col.count;
                int cidx = (col_idx_from_value(&a->cur_col, val) + (int)lrint(t*20.0)) % n;
                ci = a->cur_col.codes[cidx];
            } else {
                ci = pixel_color_code(a,i,j,x,y,t);
            }
            int want_color = (ci>=0) && !(a->cfg.transparent_ws && eg->is_space);

            if(want_color){
                if(!color_on || ci!=last_ci){
                    char esc[32]; int n=snprintf(esc,sizeof(esc),"\x1b[38;5;%dm",ci);
                    ssize_t w3=write(STDOUT_FILENO,esc,n); (void)w3;
                    color_on=1; last_ci=ci;
                }
            }else{
                if(color_on){ append_str("\x1b[0m"); color_on=0; last_ci=-2; }
            }
            emit_glyph(eg);
        }
        if(color_on) append_str("\x1b[0m");
    }
}

static void render_mandel(App *a){
    const int w=a->tw;
    const int content_h = a->th - a->info_rows;
    const double ar = (double)content_h/(double)(w>0?w:1);
    double t = now_sec() - a->t0;

    for(int j=0;j<content_h;j++){
        term_move(j+1, 1);
        int color_on=0, last_ci=-2;

        for(int i=0;i<w;i++){
            double x0 = a->cfg.cx + ( (double)i/(w-1)-0.5 ) * a->cfg.scale;
            double y0 = a->cfg.cy + ( (double)j/((content_h-1>0)?(content_h-1):1)-0.5 ) * a->cfg.scale * ar;
            double x=0,y=0; int iter=0; const int max=a->cfg.max_iter;
            while(x*x+y*y<=4.0 && iter<max){
                double xt = x*x - y*y + x0;
                y = 2*x*y + y0;
                x = xt;
                iter++;
            }
            double tval = (iter>=max)? -1.0 : (double)iter/(double)max*2.0-1.0;
            size_t idx = cs_idx_from_value(&a->acs,tval);
            const Glyph *g = &a->acs.g[idx];
            const Glyph *eg = g->is_space ? &a->bg.bg : g;

            int ci;
            if(a->cfg.color_func && a->cur_col.valid && a->cur_col.count>0){
                int n=a->cur_col.count;
                int cidx = (iter + (int)lrint(t*20.0)) % n;
                ci = a->cur_col.codes[cidx];
            } else {
                ci = pixel_color_code(a,i,j,x0,y0,t);
            }
            int want_color = (ci>=0) && !(a->cfg.transparent_ws && eg->is_space);

            if(want_color){
                if(!color_on || ci!=last_ci){
                    char esc[32]; int n=snprintf(esc,sizeof(esc),"\x1b[38;5;%dm",ci);
                    ssize_t w4=write(STDOUT_FILENO,esc,n); (void)w4;
                    color_on=1; last_ci=ci;
                }
            }else{
                if(color_on){ append_str("\x1b[0m"); color_on=0; last_ci=-2; }
            }
            emit_glyph(eg);
        }
        if(color_on) append_str("\x1b[0m");
    }
}

static void render_julia(App *a){
    const int w=a->tw;
    const int content_h = a->th - a->info_rows;
    const double ar = (double)content_h/(double)(w>0?w:1);
    double t = now_sec() - a->t0;

    for(int j=0;j<content_h;j++){
        term_move(j+1, 1);
        int color_on=0, last_ci=-2;

        for(int i=0;i<w;i++){
            double zx = a->cfg.cx + ( (double)i/(w-1)-0.5 ) * a->cfg.scale;
            double zy = a->cfg.cy + ( (double)j/((content_h-1>0)?(content_h-1):1)-0.5 ) * a->cfg.scale * ar;
            int iter=0; const int max=a->cfg.max_iter;
            while(zx*zx+zy*zy<=4.0 && iter<max){
                double xt = zx*zx - zy*zy + a->cfg.j_re;
                zy = 2*zx*zy + a->cfg.j_im;
                zx = xt;
                iter++;
            }
            double tval = (iter>=max)? -1.0 : (double)iter/(double)max*2.0-1.0;
            size_t idx = cs_idx_from_value(&a->acs,tval);
            const Glyph *g = &a->acs.g[idx];
            const Glyph *eg = g->is_space ? &a->bg.bg : g;

            int ci;
            if(a->cfg.color_func && a->cur_col.valid && a->cur_col.count>0){
                int n=a->cur_col.count;
                int cidx = (iter + (int)lrint(t*20.0)) % n;
                ci = a->cur_col.codes[cidx];
            } else {
                ci = pixel_color_code(a,i,j,zx,zy,t);
            }
            int want_color = (ci>=0) && !(a->cfg.transparent_ws && eg->is_space);

            if(want_color){
                if(!color_on || ci!=last_ci){
                    char esc[32]; int n=snprintf(esc,sizeof(esc),"\x1b[38;5;%dm",ci);
                    ssize_t w5=write(STDOUT_FILENO,esc,n); (void)w5;
                    color_on=1; last_ci=ci;
                }
            }else{
                if(color_on){ append_str("\x1b[0m"); color_on=0; last_ci=-2; }
            }
            emit_glyph(eg);
        }
        if(color_on) append_str("\x1b[0m");
    }
}

// ----------------------------- IO/helpers ----------------------------------
static int set_nonblock(int fd,int on){
    int fl = fcntl(fd,F_GETFL,0);
    if(fl<0) return -1;
    if(on) fl |= O_NONBLOCK; else fl &= ~O_NONBLOCK;
    return fcntl(fd,F_SETFL,fl);
}
static int read_key(char *out, size_t n){ return (int)read(STDIN_FILENO,out,n); }

// ----------------------------- load helpers --------------------------------
static char *read_file(const char *path){
    FILE *f=fopen(path,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=(char*)malloc(sz+1); if(!buf){ fclose(f); return NULL; }
    if(fread(buf,1,sz,f)!=(size_t)sz){ fclose(f); free(buf); return NULL; }
    buf[sz]=0; fclose(f); return buf;
}
static int load_config_from_text(App *a, const char *txt){
    Config c; set_defaults(&c);
    parse_ini(&c, txt);
    a->cfg = c;
    // (re)build derived state
    app_pick_charset(a);
    a->cached_col_idx = -9999; a->cur_col.valid = 0;
    app_init_background(a);
    return 0;
}
static int load_config_from_file(App *a, const char *path){
    char *txt = read_file(path);
    if(!txt) return -1;
    int r = load_config_from_text(a, txt);
    free(txt);
    return r;
}

static int save_function_to_file(const Config *c, const char *path){
    FILE *f=fopen(path,"wb"); if(!f) return -1;
    fprintf(f,"[expr]\nvalue=%s\ncolor=%s\n", c->expr_value, c->expr_color);
    fclose(f);
    return 0;
}
static int find_preset_index(const char *name){
    if(!name) return -1;
    for(size_t i=0;i<g_baked_presets_count;i++)
        if(strieq(g_baked_presets[i].name,name)) return (int)i;
    return -1;
}
static int load_baked_preset_by_index(App *a, int idx){
    if(idx<0 || (size_t)idx>=g_baked_presets_count) return -1;
    a->cur_preset_idx = idx;
    return load_config_from_text(a, g_baked_presets[idx].ini);
}
static int load_baked_preset(App *a, const char *name){
    int idx = find_preset_index(name);
    if(idx<0) return -1;
    return load_baked_preset_by_index(a, idx);
}

static int find_char_index(const char *name){
    if(!name) return -1;
    for(size_t i=0;i<g_char_pals_count;i++)
        if(strieq(g_char_pals[i].name,name)) return (int)i;
    return -1;
}
static int find_color_index(const char *name){
    if(!name) return -1;
    for(size_t i=0;i<g_color_pals_count;i++)
        if(strieq(g_color_pals[i].name,name)) return (int)i;
    return -1;
}
static void usage(const char *argv0){
    fprintf(stderr,
"Usage: %s [--config file] [--preset NAME] [--char NAME] [--color NAME] [--background UTF8] [--color-func]\n"
"Keys: q quit | p pause | i info | W whitespace-transparency | w cycle background | +/- fps | C toggle color | c next color | f col-math | n next char | m next function | r reload | arrows/[] pan/zoom\n",
    argv0);
    if(g_baked_presets_count){
        fprintf(stderr,"Functions:"); for(size_t i=0;i<g_baked_presets_count;i++) fprintf(stderr," %s", g_baked_presets[i].name); fprintf(stderr,"\n");
    }
    if(g_char_pals_count||g_color_pals_count){
        if(g_char_pals_count){ fprintf(stderr,"Char palettes:"); for(size_t i=0;i<g_char_pals_count;i++) fprintf(stderr," %s", g_char_pals[i].name); fprintf(stderr,"\n"); }
        if(g_color_pals_count){ fprintf(stderr,"Color palettes:"); for(size_t i=0;i<g_color_pals_count;i++) fprintf(stderr," %s", g_color_pals[i].name); fprintf(stderr,"\n"); }
    }
    fprintf(stderr,"Background cycle: ");
    for(int i=0;i<BG_CAND_COUNT;i++) fprintf(stderr,"%s%s", BG_CANDIDATES[i], (i+1<BG_CAND_COUNT)?", ":"\n");
}

// ----------------------------- main ----------------------------------------
int main(int argc, char **argv){
    App app; memset(&app,0,sizeof(app));
    set_defaults(&app.cfg);
    app.info_mode = INFO_ALL;
    app.info_rows = 0;
    app.cached_col_idx = -9999;
    app.cur_preset_idx = -1;
    app.run_mode = RUNMODE_PLAYER;
    app.editor_param = EP_FPS;
    app.editor_step_idx = 2; /* step=1 */
    app.editor_submode = ESM_DRAW;
    app.live_preview = 1;
    app.editing_text = 0; app.edit_buf[0]=0; app.edit_orig[0]=0; app.edit_len=0; app.edit_cursor=0; app.edit_target=EDIT_TARGET_EXPR;
    app.editing_tokens = 0; app.expr_tok_count=0; app.expr_tok_sel=0; app.edit_tok_idx=-1; app.pending_brace=0;
    editor_set_submode(&app, ESM_DRAW);

    const char *config_path = NULL;
    const char *preset = NULL;
    const char *char_name = NULL;
    const char *color_name = NULL;
    const char *background_arg = NULL;

    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"-c")||!strcmp(argv[i],"--config")){
            if(i+1<argc){ config_path=argv[++i]; } else { usage(argv[0]); return 1; }
        } else if(!strcmp(argv[i],"--preset")){
            if(i+1<argc){ preset=argv[++i]; } else { usage(argv[0]); return 1; }
        } else if(!strcmp(argv[i],"--char")){
            if(i+1<argc){ char_name=argv[++i]; } else { usage(argv[0]); return 1; }
        } else if(!strcmp(argv[i],"--color")){
            if(i+1<argc){ color_name=argv[++i]; } else { usage(argv[0]); return 1; }
        } else if(!strcmp(argv[i],"--background")){
            if(i+1<argc){ background_arg=argv[++i]; } else { usage(argv[0]); return 1; }
        } else if(!strcmp(argv[i],"--color-func")){
            app.cfg.color_func = 1;
        } else if(!strcmp(argv[i],"-h")||!strcmp(argv[i],"--help")){
            usage(argv[0]); return 0;
        } else {
            fprintf(stderr,"Unknown arg: %s\n", argv[i]); usage(argv[0]); return 1;
        }
    }

    // load function/preset/config
    if(config_path){
        if(load_config_from_file(&app, config_path)!=0){
            fprintf(stderr,"Failed to load config: %s\n", config_path);
            return 1;
        }
    } else if(preset){
        if(load_baked_preset(&app, preset)!=0){
            fprintf(stderr,"Preset not found: %s\n", preset);
            return 1;
        }
    } else if(g_baked_presets_count){
        load_baked_preset_by_index(&app, 0);
    } else {
        app_pick_charset(&app);
        app_init_background(&app);
    }

    if(char_name){
        int idx=find_char_index(char_name);
        if(idx<0){ fprintf(stderr,"Char palette not found: %s\n", char_name); }
        else { g_charpal_idx=idx; app_pick_charset(&app); }
    }
    if(color_name){
        int idx=find_color_index(color_name);
        if(idx<0){ fprintf(stderr,"Color palette not found: %s\n", color_name); }
        else { g_colorpal_idx=idx; }
    }
    if(background_arg){
        // override background (UTF-8). Use exactly what was passed; for " " you get edges-only.
        strncpy(app.cfg.background_utf8, background_arg, sizeof(app.cfg.background_utf8)-1);
        app.cfg.background_utf8[sizeof(app.cfg.background_utf8)-1]=0;
        app_init_background(&app);
    }

    colorpal_from_selection(&app.cur_col);

    signal(SIGWINCH,on_winch);
    term_raw_on(); atexit(term_raw_off);
    term_alt_on(); atexit(term_alt_off);
    term_wrap_off(); atexit(term_wrap_on);
    term_hide_cursor(); atexit(term_show_cursor);
    term_clear();
    set_nonblock(STDIN_FILENO,1);

    double start = now_sec();
    app.t0 = start;

    for(;;){
        if(g_resized){ g_resized=0; app_query_size(&app); term_clear(); }
        app_query_size(&app);

        if(app.cached_col_idx != g_colorpal_idx){
            colorpal_from_selection(&app.cur_col);
            app.cached_col_idx = g_colorpal_idx;
        }

        int fps = app.cfg.fps<=0?30:app.cfg.fps;
        int frame_ms = (int)lrint(1000.0 / (double)fps);

        double t = app.paused ? (app.t0 - start) : (now_sec() - start);
        if(app.cfg.duration_ms>=0 && (long)lrint(t*1000.0) >= app.cfg.duration_ms) break;

        // input
        char keys[64]; int n=read_key(keys,sizeof(keys));
        for(int k=0;k<n;k++){
            unsigned char c=keys[k];
            if(app.run_mode==RUNMODE_PLAYER){
                if(c==0x14){ app.run_mode=RUNMODE_EDITOR; }
                else if(c=='q') goto out;
                else if(c=='p'){ app.paused = !app.paused; if(!app.paused) start = now_sec() - (app.t0 - start); }
                else if(c=='i'){ app.info_mode = (app.info_mode + 1) % 3; }
                else if(c=='W'){ app.cfg.transparent_ws = !app.cfg.transparent_ws; }
                else if(c=='w'){ bg_cycle_next(&app.bg); }
                else if(c=='+'){ app.cfg.fps = clamp_long(app.cfg.fps+1,1,240); }
                else if(c=='-'){ app.cfg.fps = clamp_long(app.cfg.fps-1,1,240); }
                else if(c=='C'){ app.cfg.use_color = !app.cfg.use_color; }
                else if(c=='c'){ if(g_color_pals_count){ g_colorpal_idx = (g_colorpal_idx+1) % (int)g_color_pals_count; app.cfg.use_color=1; } }
                else if(c=='f'){ app.cfg.color_func = !app.cfg.color_func; }
                else if(c=='n'){
                    if(g_char_pals_count){ g_charpal_idx = (g_charpal_idx+1) % (int)g_char_pals_count; app_pick_charset(&app); }
                    else { g_charpal_idx = -1; g_charpal_fb_idx = (g_charpal_fb_idx+1)%4; app_pick_charset(&app); }
                }
                else if(c=='m'){ if(g_baked_presets_count){ int next=(app.cur_preset_idx>=0)?(app.cur_preset_idx+1)%(int)g_baked_presets_count:0; load_baked_preset_by_index(&app,next); app_pick_charset(&app); app_init_background(&app); } }
                else if(c=='r'){
                    if(config_path){ load_config_from_file(&app, config_path); app_pick_charset(&app); app.cached_col_idx=-9999; app_init_background(&app); }
                    else if(app.cur_preset_idx>=0){ load_baked_preset_by_index(&app, app.cur_preset_idx); app_pick_charset(&app); app.cached_col_idx=-9999; app_init_background(&app); }
                }
                else if(c==0x1b){
                    if(k+2<n && keys[k+1]=='['){
                        char d=keys[k+2];
                        if(app.cfg.mode==MODE_MANDELBROT || app.cfg.mode==MODE_JULIA){
                            double pan = app.cfg.scale*0.05;
                            if(d=='A') app.cfg.cy -= pan;
                            if(d=='B') app.cfg.cy += pan;
                            if(d=='C') app.cfg.cx += pan;
                            if(d=='D') app.cfg.cx -= pan;
                        }
                        k+=2;
                    }
                } else if(c=='[' || c==']'){
                    if(app.cfg.mode==MODE_MANDELBROT || app.cfg.mode==MODE_JULIA){
                        if(c==']') app.cfg.scale *= 0.9;
                        else app.cfg.scale *= 1.1;
                    }
                }
            }else{ /* editor mode */
                if(c==0x1b && k+1<n && keys[k+1] != '['){
                    char d=keys[k+1];
                    if(d=='c' || d=='C'){ editor_set_submode(&app, ESM_COLOR); k++; continue; }
                    else if(d=='d' || d=='D'){ editor_set_submode(&app, ESM_DRAW); k++; continue; }
                    else if(d=='v' || d=='V'){
                        if(app.editing_text){
                            if(app.edit_target==EDIT_TARGET_EXPR){
                                validate_expr_string(app.edit_buf);
                                app.edit_len=strlen(app.edit_buf);
                                if(app.live_preview) apply_edit_text(&app,0);
                            }
                        }else{
                            if(app.editing_tokens) editor_tokens_to_expr(&app);
                            validate_expr_string(current_expr(&app));
                            if(app.editing_tokens){
                                int sel=app.expr_tok_sel;
                                app.expr_tok_count = tokenize_expr(current_expr(&app), app.expr_tokens, MAX_TOKENS);
                                if(app.expr_tok_count==0) app.expr_tok_sel=0;
                                else if(sel>=app.expr_tok_count) app.expr_tok_sel=app.expr_tok_count-1;
                                else app.expr_tok_sel=sel;
                            }
                        }
                        k++; continue;
                    } else if(!app.editing_text && (d=='s' || d=='S')){
                        if(app.editing_tokens) editor_exit_token_mode(&app);
                        start_path_edit(&app,0);
                        k++; continue;
                    } else if(!app.editing_text && (d=='i' || d=='I')){
                        if(app.editing_tokens) editor_exit_token_mode(&app);
                        start_path_edit(&app,1);
                        k++; continue;
                    }
                }
                if(app.editing_text){
                    if(c==0x19 || c=='\r' || c=='\n'){ apply_edit_text(&app,1); }
                    else if(c==0x12 && app.edit_target==EDIT_TARGET_EXPR){ apply_edit_text(&app,0); }
                    else if(c==0x18){ cancel_edit_text(&app); }
                    else if(c==0x0c && app.edit_target==EDIT_TARGET_EXPR){ app.live_preview = !app.live_preview; if(app.live_preview) apply_edit_text(&app,0); }
                    else if(c==0x7f){
                        if(app.edit_cursor>0){
                            memmove(app.edit_buf+app.edit_cursor-1, app.edit_buf+app.edit_cursor, app.edit_len - app.edit_cursor +1);
                            app.edit_cursor--; app.edit_len--;
                            if(app.live_preview) apply_edit_text(&app,0);
                        }
                    }else if(c==0x1b){
                        if(k+2<n && keys[k+1]=='['){
                            char d=keys[k+2];
                            if(d=='C' && app.edit_cursor<app.edit_len) app.edit_cursor++;
                            else if(d=='D' && app.edit_cursor>0) app.edit_cursor--;
                            k+=2;
                        }
                    }else if(c>=32 && c<127){
                        if(app.edit_len<sizeof(app.edit_buf)-1){
                            memmove(app.edit_buf+app.edit_cursor+1, app.edit_buf+app.edit_cursor, app.edit_len - app.edit_cursor +1);
                            app.edit_buf[app.edit_cursor]=c;
                            app.edit_cursor++; app.edit_len++;
                            if(app.live_preview) apply_edit_text(&app,0);
                        }
                    }
                }else if(app.editing_tokens){
                    if(app.pending_brace){
                        if(c=='('||c=='['||c=='{'){ insert_brace_block(&app,c); app.pending_brace=0; continue; }
                        app.pending_brace=0;
                    }
                    if(c==0x14){ app.run_mode=RUNMODE_PLAYER; }
                    else if(c=='i'){ app.info_mode = (app.info_mode + 1) % 3; }
                    else if(c==0x05){ start_text_edit(&app,-1); }
                    else if(c=='e'){ start_text_edit(&app, app.expr_tok_sel); }
                    else if(c==0x0c){ app.live_preview = !app.live_preview; if(app.live_preview) editor_tokens_to_expr(&app); }
                    else if(c==0x0f){ remove_brace_block(&app); }
                    else if(c==0x10){ app.pending_brace=1; }
                    else if(c==0x7f){
                        Token *t=&app.expr_tokens[app.expr_tok_sel];
                        size_t len=strlen(t->text);
                        if(len>0){
                            t->text[len-1]=0;
                            t->type=classify_token_text(t->text);
                            if(app.live_preview) editor_tokens_to_expr(&app);
                        }
                    }else if(c>=32 && c<127){
                        Token *t=&app.expr_tokens[app.expr_tok_sel];
                        size_t len=strlen(t->text);
                        if(len+1 < sizeof(t->text)){
                            t->text[len]=c;
                            t->text[len+1]=0;
                            t->type=classify_token_text(t->text);
                            if(app.live_preview) editor_tokens_to_expr(&app);
                        }
                    }
                    else if(c==0x1b){
                        if(k+2<n && keys[k+1]=='['){
                            char d=keys[k+2];
                            if(d=='C') app.expr_tok_sel = (app.expr_tok_sel+1)%app.expr_tok_count;
                            else if(d=='D') app.expr_tok_sel = (app.expr_tok_sel+app.expr_tok_count-1)%app.expr_tok_count;
                            else if(d=='A') { editor_adjust_token(&app,+1); if(app.live_preview) editor_tokens_to_expr(&app); }
                            else if(d=='B') { editor_adjust_token(&app,-1); if(app.live_preview) editor_tokens_to_expr(&app); }
                            k+=2;
                        }
                    }else if(c=='\r' || c=='\n'){ editor_exit_token_mode(&app); }
                }else{
                    if(c==0x14){ app.run_mode=RUNMODE_PLAYER; }
                    else if(c=='i'){ app.info_mode = (app.info_mode + 1) % 3; }
                    else if(c=='+'){ editor_adjust_param(&app,+1); }
                    else if(c=='-'){ editor_adjust_param(&app,-1); }
                    else if(c=='['){ if(app.editor_step_idx>0) app.editor_step_idx--; }
                    else if(c==']'){ if(app.editor_step_idx+1<EDIT_STEP_COUNT) app.editor_step_idx++; }
                    else if(c==0x05){ start_text_edit(&app,-1); }
                    else if((c=='\r' || c=='\n') && app.editor_param==EP_EXPR){ editor_enter_token_mode(&app); }
                    else if(c==0x1b){
                        if(k+2<n && keys[k+1]=='['){
                            char d=keys[k+2];
                            if(d=='C') app.editor_param = (app.editor_param+1)%EP_COUNT;
                            else if(d=='D') app.editor_param = (app.editor_param+EP_COUNT-1)%EP_COUNT;
                            else if(d=='A') editor_adjust_param(&app,+1);
                            else if(d=='B') editor_adjust_param(&app,-1);
                            k+=2;
                        }
                    }
                }
            }
        }
        update_info_rows(&app);
        // draw
        if(app.cfg.mode==MODE_EXPR) render_expr(&app, t);
        else if(app.cfg.mode==MODE_MANDELBROT) render_mandel(&app);
        else render_julia(&app);

        draw_info_bar(&app);
        msleep(frame_ms);
    }

out:
    term_clear();
    return 0;
}
