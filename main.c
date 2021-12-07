/* See "license" file for copyright and license details.
* Dynamic window manager is designed like any other X client as well. It is
* driven through handling X events. In contrast to other X clients, a window
* manager selects for SubstructureRedirectMask on the root window, to receive
* Events about window (dis-)appearance. Only one X connection at a time is
* allowed to select for this event mask.
* 
* The event handlers of dwm(xvvm) are organized in an array which is accessed
* whenever a new event has been fetched. This allows event dispatching
* in O(1) time.
*
* Each child of the root window is called a client, except windows which haveset the override_redirect flag.
* Clients are organized in a linked client
* list on each monitor, the focus history is remembered through a stack list
* on each monitor. Each client contains a bit array to indicate the tags of a
* client.
*
* Keys and tagging rules are organized as arrays and defined in "config.h".
*
* To understand everything else, start reading "main()". */

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* Macros. */
#define BUTTONMASK (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask) (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m) (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C) ((C->tags & C->mon->tagset[C->mon->seltags]))

#define LENGTH(X) (sizeof X / sizeof X[0]) /* Length of any array. */
#define MOUSEMASK (BUTTONMASK|PointerMotionMask)
#define WIDTH(X) ((X)->w + 2 * (X)->bw) /* Width and height considering it's borders size. */
#define HEIGHT(X) ((X)->h + 2 * (X)->bw)
#define TAGMASK ((1 << LENGTH(tags)) - 1)
#define TEXTW(X) (drw_fontset_getwidth(drw, (X)) + lrpad)
#define SIZEL(X) (sizeof((X)[0]))
#define MASK(X) (1>>(X))
/* Enums. */
enum { CurNormal, CurResize, CurMove, CurLast } ; /* Cursor */
enum { SchemeNorm, SchemeSel } ; /* Color schemes. */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast } ; /* EWMH atoms. */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast } ; /* Default atoms. */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast } ; /* Clicks. */
enum {LayoutFloating, LayoutTile, LayoutMonocle, LayoutSplit, LayoutRootwin, LayoutLast} ;
enum {SideNo, SideRight, SideLeft, SideUp, SideDown} ;
enum {
	IsAny = ~0,
	IsFree = 1<<0,
	IsTile = 1<<2,
	IsVisible = 1<<1,
	IsFullscreen = 1<<3,
	IsUrgent = 1<<4,
	IsFixed = 1<<5,
	IsInvisible = 1<<6,
} ;

typedef unsigned int uint;

typedef struct {
	int lt;
	float mfact;
	int nmaster;
} SetupLayout ;

/* Used to define users functions behavior. */
typedef struct {
	int i;
	uint ui;
	int b;
	float f;
	const void *v;
} Arg ;

/* Buttons behaviour defining structure. */
typedef struct {
	uint click;
	uint mask;
	uint button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button ;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y;
	uint w, h;
	int freex, freey;
	uint freew, freeh;
	int oldx, oldy;
	uint oldw, oldh;
	int fx, fy;
	uint fw, fh;
	uint basew, baseh, incw, inch, maxw, maxh, minw, minh;
	uint bw, oldbw; /* Border width in pixels. */
	uint tags;
	uint isfixed, isfree, isurgent, neverfocus, oldstate, isfullscreen;
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win;
} ;

/* Keys behaviour defining structure. */
typedef struct {
	uint mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key ;

typedef struct {
	int side;
	void (*func)(const Arg *);
	const Arg arg;
} Side ;

/* Structure defining function and string for layouts. */
typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout ;

struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int by; /* Bar geometry. */
	int mx, my, mw, mh; /* Screen size. */
	int wx, wy, ww, wh; /* Window area.  */
	uint seltags;
	uint tagset[2];
	SetupLayout taglt[9];
	uint viewtag;
	int showbar;
	int topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	const Layout *lt;
} ;

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	uint tags;
	int isfree;
	int monitor;
} Rule ;

/* Function declarations. */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static Client* clientclick(uint waitRelease, uint returnCurrentIfNoChoosen);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void floating(Monitor *m);
static void focus(Client *c);
static void focuscurwin(const Arg *arg);
static void raiseclient(Client *c);
static void raisefocused(const Arg *arg);
static void lowerclient(Client *c);
static void lowerfocused(const Arg *arg);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char *text, uint size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killcurclient(const Arg *arg);
static void killclick(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static void moveclick(const Arg *arg);
static void moveWins(Monitor *m, int dx, int dy);
static uint nClients(Monitor *m, uint m);
static Client *nextclient(Client *c, uint m);
static uint ckclient(Client *c, uint m);
static void nextlayout(const Arg *arg);
static void pop(Client *);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void rootwin(Monitor *m);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizeclick(const Arg *arg);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static int sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setOldGeometry(Client *c, int x, int y, uint w, uint h);
static void setFloatingGeometry(Client *c, int x, int y, uint w, uint h);
static void setFreeGeometry(Client *c, int x, int y, uint w, uint h);
static void setGeometry(Client *c, int x, int y, uint w, uint h);
static void setWCGeometry(XWindowChanges *wc, int x, int y, uint w, uint h);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void scrolldesk(Monitor *m, int dx, int dy, int mvptr);
static void scrolldeskhorizontal(const Arg *arg);
static void scrolldeskvertical(const Arg *arg);
static void sidehandle(void);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *);
static void split(Monitor *);
static void togglebar(const Arg *arg);
static void togglefree(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static void viewnext(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int waitmouse(XEvent *ev, int type);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);
uint isCancelClickGestureDone(void);
static void togglefullscreen(const Arg *arg);

/* Variables. */
static const char broken[] = "broken" ;
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height. */
static int bh, blw = 0;      /* Bar geometry. */
static int lrpad;            /* Sum of left and right padding for text. */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static uint numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static int restart = 0 ;
static int running = 1 ;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static uint nmons = 0 ;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;
static char *argv0;

/* Configuration, allows nested code to access above variables. */
#include "config.h"

/* Compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* Function implementations. */

void
applyrules(Client *c)
{
	const char *class, *instance;
	uint i;
	const Rule *r;
	Monitor *m;
	XClassHint ch = { NULL, NULL };

	/* Rule matching. */
	c->isfree = 0;
	c->tags = 0;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isfree = r->isfree;
			c->tags |= r->tags;
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
		}
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon ;

	/* Set minimum possible. */
	*w = MAX(1, *w) ;
	*h = MAX(1, *h) ;
	if(interact){
		if (*x > sw)
			*x = sw - WIDTH(c) ;
		if (*y > sh)
			*y = sh - HEIGHT(c) ;
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	}else{
		if(m->lt!=&layouts[LayoutFloating]){
			if (*x >= m->wx + m->ww ){
				*x = m->wx + m->ww - WIDTH(c) ;
			}
			if (*y >= m->wy + m->wh){
				*y = m->wy + m->wh - HEIGHT(c) ;
			}
			if (*x + *w + 2 * c->bw <= m->wx){
				*x = m->wx;
			}
			if (*y + *h + 2 * c->bw <= m->wy){
				*y = m->wy;
			}
		}
	}
	if (*h < bh){
		*h = bh ;
	}
	if (*w < bh){
		*w = bh ;
	}
	if (resizehints || c->isfree || !c->mon->lt->arrange) {
		/* See last two sentences in "ICCCM 4.1.2.3". */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* Temporarily remove base dimensions. */
			*w -= c->basew ;
			*h -= c->baseh ;
		}
		/* Adjust for aspect limits. */
		if( c->mina > 0 && c->maxa > 0 ){
			if (c->maxa < (float)*w / *h){
				*w = *h * c->maxa + 0.5 ;
			}else if( c->mina < (float)*h / *w ){
				*h = *w * c->mina + 0.5 ;
			}
		}
		if (baseismin) { /* Increment calculation requires this. */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* Adjust for increment value. */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* Restore base dimensions. */
		*w = MAX(*w + c->basew, c->minw) ;
		*h = MAX(*h + c->baseh, c->minh) ;
		if (c->maxw){
			*w = MIN(*w, c->maxw) ;
		}
		if (c->maxh){
			*h = MIN(*h, c->maxh) ;
		}
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h ;
}

void
arrange(Monitor *m)
{
	if (m){
		showhide(m->stack);
	}else{
		for( m = mons ; m ; m = m->next ){
			showhide(m->stack);
		}
	}
	if( m ){
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next){
		arrangemon(m);
	}
}

void
arrangemon(Monitor *m)
{
	strncpy(m->ltsymbol, m->lt->symbol, sizeof m->ltsymbol);
	if (m->lt->arrange) m->lt->arrange(m) ;
	
}

void
attach(Client *c)
{
	c->next = c->mon->clients ;
	c->mon->clients = c ;
}

void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void
buttonpress(XEvent *e)
{
	uint i, x, click;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClkRootWin;
	/* Focus monitor if necessary. */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
		i = x = 0;
		do
			x += TEXTW(tags[i]);
		while (ev->x >= x && ++i < LENGTH(tags));
		if (i < LENGTH(tags)) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		} else if (ev->x < x + blw)
			click = ClkLtSymbol;
		else if (ev->x > selmon->ww - TEXTW(stext))
			click = ClkStatusText;
		else
			click = ClkWinTitle;
	} else if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClkClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
				&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

Client *
clientclick(uint waitButtonRelease, uint returnCurrentIfNoChoosen)
{
	Client *c;
	int x, y;
	Window win;
	XEvent ev;
	
	if( XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
			None, cursor[CurMove]->cursor, CurrentTime)
			!= GrabSuccess)
		return 0 ;
		
	do{
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch( ev.type ){
		case ConfigureRequest :
		case Expose :
		case MapRequest :
			handler[ev.type](&ev);
		case MotionNotify:
			sidehandle();
		break;
		}
	}while( ev.type != ButtonPress );
	if(waitButtonRelease)
		waitmouse(&ev, ButtonRelease);
	
	XUngrabPointer(dpy, CurrentTime) ;

	win = ev.xbutton.subwindow ;
	if( isCancelClickGestureDone() || win == root )
		return 0 ;
	if( !(c = wintoclient(win)) && returnCurrentIfNoChoosen )
		c = selmon->sel ;

	return c ;
}

uint
isCancelClickGestureDone(void)
{
	int x, y;
	if(getrootptr(&x, &y) && !x && !y)
		return 1 ;
	return 0 ;
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* This causes an error if some other window manager is running. */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;

	view(&a);
	selmon->lt = &foo;
	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		free(scheme[i]);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
	free(mon);
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (c != selmon->sel && !c->isurgent)
			seturgent(c, 1);
	}
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy ;
	ce.event = c->win ;
	ce.window = c->win ;
	ce.x = c->x ;
	ce.y = c->y ;
	ce.width = c->w ;
	ce.height = c->h ;
	ce.border_width = c->bw ;
	ce.above = None ;
	ce.override_redirect = False ;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified. */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width ;
		sh = ev->height ;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next){
					if (c->isfullscreen){
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
					}
				}
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest ;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth){
			c->bw = ev->border_width;
		}else if( c->isfree || !selmon->lt->arrange ){
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y ;
				c->y = m->my + ev->y ;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w ;
				c->w = ev->width ;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h ;
				c->h = ev->height ;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfree)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* Center in X direction. */
			if ((c->y + c->h) > m->my + m->mh && c->isfree)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* Center in Y direction. */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		}else{
			configure(c);
		}
	}else{
		wc.x = ev->x ;
		wc.y = ev->y ;
		wc.width = ev->width ;
		wc.height = ev->height ;
		wc.border_width = ev->border_width ;
		wc.sibling = ev->above ;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void)
{
	Monitor *m;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1 ;
	m->mfact = mfact ;
	m->nmaster = nmaster ;
	m->showbar = showbar ;
	m->topbar = topbar ;
	m->lt = &layouts[setup_layouts[0].lt] ;
	strncpy(m->ltsymbol, layouts[setup_layouts[0].lt].symbol, sizeof m->ltsymbol);
	for( int i=0 ; i<9 ; ++i ){
		m->taglt[i].lt = setup_layouts[i].lt ;
		m->taglt[i].mfact = setup_layouts[i].mfact ;
		m->taglt[i].nmaster = setup_layouts[i].nmaster ;
	}
	++nmons;
	return m ;
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = 0 ;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}

void
drawbar(Monitor *m)
{
	int x, w, sw = 0;
	int boxs = drw->fonts->h / 9;
	int boxw = drw->fonts->h / 6 + 2;
	uint i, occ = 0, urg = 0;
	Client *c;

	/* Draw status first so it can be overdrawn by tags later. */
	if (m == selmon) { /* Status is only drawn on selected monitor. */
		drw_setscheme(drw, scheme[SchemeNorm]);
		sw = TEXTW(stext) - lrpad + 2; /* 2px right padding. */
		drw_text(drw, m->ww - sw, 0, sw, bh, 0, stext, 0);
	}

	for (c = m->clients; c; c = c->next) {
		occ |= c->tags;
		if (c->isurgent)
			urg |= c->tags;
	}
	x = 0;
	for (i = 0; i < LENGTH(tags); i++) {
		w = TEXTW(tags[i]);
		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
		drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
		if (occ & 1 << i)
			drw_rect(drw, x + boxs, boxs, boxw, boxw,
				m == selmon && selmon->sel && selmon->sel->tags & 1 << i,
				urg & 1 << i);
		x += w;
	}
	w = blw = TEXTW(m->ltsymbol);
	drw_setscheme(drw, scheme[SchemeNorm]);
	x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

	if ((w = m->ww - sw - x) > bh) {
		if (m->sel) {
			drw_setscheme(drw, scheme[m == selmon ? SchemeSel : SchemeNorm]);
			drw_text(drw, x, 0, w, bh, lrpad / 2, m->sel->name, 0);
			if (m->sel->isfree)
				drw_rect(drw, x + boxs, boxs, boxw, boxw, m->sel->isfixed, 0);
		} else {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_rect(drw, x, 0, w, bh, 1, 1);
		}
	}
	drw_map(drw, m->barwin, 0, 0, m->ww, bh);
}

void
drawbars(void)
{
	Monitor *m;

	for (m = mons; m; m = m->next)
		drawbar(m);
}

void
enternotify(XEvent *e)
{
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
	} else if (!c || c == selmon->sel)
		return;
	focus(c);
}

void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);
}

void
floating(Monitor *m)
{
	Client *c;
	/* if( !nClients(m, IsVisible|IsTile) ) return ; */
	for( c=nextclient(m->clients, IsTile|IsVisible) ; c ; c=nextclient(c->next, IsTile|IsVisible) )
		resize(c, c->fx, c->fy, c->fw, c->fh, 0);
}

void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c))
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		setfocus(c);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

/* There are some broken focus acquiring clients needing extra handling. */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;
	Client *c;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
	if(c = selmon->sel)
		XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w/2, c->h/2);
}

void
focuscurwin(const Arg *arg)
{
	focus(selmon->sel);
}

void
raiseclient(Client *c)
{
	XRaiseWindow(dpy, c->win);
}

void
raisefocused(const Arg *arg)
{
	raiseclient(selmon->sel);
}

void
lowerclient(Client *c)
{
	XLowerWindow(dpy, c->win);
}

void
lowerfocused(const Arg *arg)
{
	lowerclient(selmon->sel);
}

void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;
	if(! selmon->sel ) return ;
	if( arg->i > 0 ){
		for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
	}else{
		for (i = selmon->clients; i != selmon->sel; i = i->next)
			if (ISVISIBLE(i))
				c = i;
		if (!c) for (; i; i = i->next) if (ISVISIBLE(i)) c = i ;
	}
	if(c){
		focus(c);
		if(arg->b && (c = selmon->sel))
			XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w/2, c->h/2);
		restack(selmon);
	}
}

Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

int
getrootptr(int *x, int *y)
{
	int di;
	uint dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

int
gettextprop(Window w, Atom atom, char *text, uint size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING)
		strncpy(text, (char *)name.value, size - 1);
	else {
		if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
			strncpy(text, *list, size - 1);
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		uint i, j;
		uint modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void)
{
	updatenumlockmask();
	{
		uint i, j;
		uint modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		KeyCode code;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		for (i = 0; i < LENGTH(keys); i++)
			if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
						True, GrabModeAsync, GrabModeAsync);
	}
}

void
incnmaster(const Arg *arg)
{
	Monitor *m = selmon ;
	int n, i, newval;
	i = arg->i ;

	if(!i){
		m->taglt[m->viewtag].nmaster = m->nmaster = 1 ;
	}else{ 
		n = nClients(m, IsTile|IsVisible) ;
		newval = m->nmaster + i ;
		if(n<newval){
			m->taglt[m->viewtag].nmaster =
				m->nmaster = 1 ;
		} else if(newval<0){
			m->taglt[m->viewtag].nmaster =
				m->nmaster = n + i ;
		} else {
			m->taglt[m->viewtag].nmaster =
				m->nmaster = MAX(m->nmaster + arg->i, 0) ;
		}
	}

	arrange(selmon);
}

#ifdef XINERAMA
static
int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
	uint i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
killcurclient(const Arg *arg)
{
	if (!selmon->sel)
		return;
	if (!sendevent(selmon->sel, wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL ;
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client)) ;
	c->win = w ;
	/* Geometry. */
	c->x = c->freex = c->oldx = c->fx = wa->x ;
	c->y = c->freey = c->oldy = c->fy = wa->y ;
	c->w = c->freew = c->oldw = c->fw = wa->width ;
	c->h = c->freeh = c->oldh = c->fh = wa->height ;
	c->oldbw = wa->border_width ;


	updatetitle(c);
	if( XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans)) ){
		c->mon = t->mon ;
		c->tags = t->tags ;
	}else{
		c->mon = selmon ;
		applyrules(c);
	}

	if (c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
		c->x = c->mon->mx + c->mon->mw - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
		c->y = c->mon->my + c->mon->mh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->mx);
	/* Only fix client y-offset, if the client center might cover the bar. */
	c->y = MAX(c->y, ((c->mon->by == c->mon->my) && (c->x + (c->w / 2) >= c->mon->wx)
		&& (c->x + (c->w / 2) < c->mon->wx + c->mon->ww)) ? bh : c->mon->my);
	c->bw = borderpx ;

	wc.border_width = c->bw ;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* Propagates border_width, if size doesn't change. */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	if( c->isfree ){
		raiseclient(c);
	}else{
		c->isfree = c->oldstate = trans != None || c->isfixed ;
	}
	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1 );
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* Some windows require this. */
	setclientstate(c, NormalState);
	if (c->mon == selmon) unfocus(selmon->sel, 0) ;
	c->mon->sel = c ;
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	focus(NULL);
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping ;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard){
		grabkeys();
	}
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest ;

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect){
		return;
	}
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
monocle(Monitor *m)
{
	uint n = 0;
	Client *c;

	for (c = m->clients; c; c = c->next){
		if (ISVISIBLE(c)){ n++ ; }
	}
	if (n > 0){ /* Override layout symbol. */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	}
	for (c = nextclient(m->clients, IsTile|IsVisible); c;
			 c = nextclient(c->next, IsTile|IsVisible) ){
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
	}
}

void
motionnotify(XEvent *e)
{
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion ;

	if( ev->window != root ) return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel, 1);
		selmon = m ;
		focus(NULL);
	}
	mon = m ;
}

void
movemouse(const Arg *arg)
{
	int x, y, nx, ny;
	Client *c;
	Monitor *m = selmon ;
	XEvent ev;

	if ( !(c = m->sel) || c->isfullscreen) return; 

	restack(m);

	/* Out of possible cursor position preventing. */
	if( c->x + c->bw > 0
			&& c->y + c->bw > 0 )
		XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, -c->bw, -c->bw);

	if (!getrootptr(&x, &y)) return;

	waitmouse(&ev, ButtonRelease);
	nx = ev.xbutton.x ; ny = ev.xbutton.y ;

	if ( !c->isfree && m->lt != &layouts[LayoutFloating] )
		c->isfree = 1 ;

	resize(c, nx, ny, c->w, c->h, 1);

	if(m->lt !=&layouts[LayoutFloating] && c->isfree)
		arrange(m);

	XUngrabPointer(dpy, CurrentTime);
}

void
moveclick(const Arg *arg)
{
	int x, y;
	Client *c;
	XEvent ev;
	getrootptr(&x, &y);
	//waitmouse(&ev, ButtonRelease);
	if(! (c = clientclick(0, 1)) ) return ;
	focus(c);
	movemouse(0);
	XWarpPointer(dpy, None, root, 0, 0, 0, 0, x, y);
}

void
killclick(const Arg *arg)
{
	int x, y;
	Client *c;
	XEvent ev;
	getrootptr(&x, &y);
	//waitmouse(&ev, ButtonRelease);
	if(! (c = clientclick(0, 1)) ) return ;
	focus(c);
	killcurclient(0);
	XWarpPointer(dpy, None, root, 0, 0, 0, 0, x, y);
}

Client *
nextclient(Client *c, uint m)
{
	for (; c && !ckclient(c, m) ; c = c->next)
		;
	return c;
}

uint
ckclient(Client *c, uint m)
{
	if(m&IsTile && c->isfree
		|| m&IsFree && !c->isfree
		|| m&IsVisible && !ISVISIBLE(c)
		|| m&IsInvisible && ISVISIBLE(c)
		|| m&IsFullscreen && !c->isfullscreen 
		|| m&IsUrgent && !c->isurgent
		|| m&IsFixed && !c->isfixed)
		return 0 ;
	return 1 ;
}

static void
nextlayout(const Arg *arg)
{
	Monitor *m = selmon ;
	Arg varg;
	int lt = ( m->lt - layouts + arg->i )   ;
	if( lt<0 ){
		lt = LENGTH(layouts) - 1 ;
	}else{
		lt %= LENGTH(layouts) ;
	}
	
	varg.v = &layouts[lt] ;
	setlayout(&varg);
}

void
pop(Client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* Ignore. */
	else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfree && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfree = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			updatesizehints(c);
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if (c == c->mon->sel)
				drawbar(c->mon);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
quit(const Arg *arg)
{
	if(arg->i) restart = 1 ;
	running = 0 ;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;
	setWCGeometry(&wc, x, y, w, h);
	setGeometry(c, x, y, w, h);
	setOldGeometry(c, x, y, w, h);
	if( c->isfree ){
		setFreeGeometry(c, x, y, w, h);
		setFloatingGeometry(c, x, y, w, h);
	} else if( c->mon->lt == &layouts[LayoutFloating] )
		setFloatingGeometry(c, x, y, w, h);
	wc.border_width = c->bw ;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
	int nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;

	if (!(c = selmon->sel) || c->isfullscreen) return ;

	restack(selmon);

	/*if( XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
			None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess )
		return; */

	if( c->x+c->w < sw && c->y+c->h < sh )
		XWarpPointer(dpy, None, c->win,
			0, 0, 0, 0,
			c->w - c->bw, c->h - c->bw);

	waitmouse(&ev, ButtonRelease) ;
	nw = MAX(1, ev.xbutton.x - c->x ) ; nh = MAX(1, ev.xbutton.y - c->y ) ;

	if( c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy
			&& c->mon->wy + nh <= selmon->wy + selmon->wh )
		if( !c->isfree && selmon->lt != &layouts[LayoutFloating] )
			c->isfree = 1 ;

	resize(c, c->x, c->y, nw, nh, 1);
	if( selmon->lt != &layouts[LayoutFloating] && c->isfree )
		arrange(selmon);

	/* To prevent increasing window width and height when just clicking. */
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
resizeclick(const Arg *arg)
{
	int x, y;
	XEvent ev;
	Client *c;
	getrootptr(&x, &y);
	//waitmouse(&ev, ButtonRelease);
	if(! (c = clientclick(0, 1)) ) return ;
	focus(c);
	resizemouse(NULL);
	XWarpPointer(dpy, None, root, 0, 0, 0, 0, x, y);
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel)
		return;
	if( (m->sel->isfree || !m->lt->arrange)
			&& m->lt != &layouts[LayoutFloating] ){
		raiseclient(m->sel);
	}
	if( m->lt->arrange ){
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (c = m->stack; c; c = c->snext)
			if (!c->isfree && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	XSync(dpy, False);
	while(XCheckMaskEvent(dpy, EnterWindowMask, &ev))
		;
}

void
run(void)
{
	XEvent ev;
	/* Main event loop. */
	XSync(dpy, False);
	while( running && !XNextEvent(dpy, &ev) ){
		if (handler[ev.type]){
			handler[ev.type](&ev); /* Call handler. */
		}
	}
}

void
sigHup(int unused)
{
	Arg a = {.i = 1} ;
	quit(&a);
}

void
sigTerm(int unused)
{
	Arg a = {.i = 0} ;
	quit(&a);
}

void
setWCGeometry(XWindowChanges *wc, int x, int y, uint w, uint h)
{
	if(!wc) return ;
	wc->x = x ; wc->y = y ; wc->width = w ; wc->height = h ;
}

void
setGeometry(Client *c, int x, int y, uint w, uint h)
{
	if(!c) return ;
	c->x = x ; c->y = y ;
	c->w = w ; c->h = h ;
}

void
setFloatingGeometry(Client *c, int fx, int fy, uint fw, uint fh)
{
	if(!c) return ;
	c->fx = fx ; c->fy = fy ;
	c->fw = fw ; c->fh = fh ;
}

void
setFreeGeometry(Client *c, int x, int y, uint w, uint h)
{
	if(!c) return ;
	c->freex = x ; c->freey = y ;
	c->freew = w ; c->freeh = h ;
}

void
setOldGeometry(Client *c, int oldx, int oldy, uint oldw, uint oldh)
{
	if(!c) return ;
	c->oldx = oldx ; c->oldy = oldy ;
	c->oldw = oldw ; c->oldh = oldh ;
}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL ;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
					|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1)){
				continue;
			}
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState){
				manage(wins[i], &wa);
			}
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m){ return; }
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->mon = m ;
	c->tags = m->tagset[m->seltags] ; /* Assign tags of target monitor. */
	attach(c);
	attachstack(c);
	focus(NULL);
	arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--){
			exists = protocols[n] == proto;
		}
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage ;
		ev.xclient.window = c->win ;
		ev.xclient.message_type = wmatom[WMProtocols] ;
		ev.xclient.format = 32 ;
		ev.xclient.data.l[0] = proto ;
		ev.xclient.data.l[1] = CurrentTime ;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *c)
{
	if (!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(c->win), 1);
	}
	sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		c->oldstate = c->isfree ;
		c->oldbw = c->bw ;
		c->bw = 0;
		c->isfree = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		raiseclient(c);
	} else if (!fullscreen && c->isfullscreen){
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = 0 ;
		c->isfree = c->oldstate ;
		c->bw = c->oldbw ;
		c->x = c->oldx ;
		c->y = c->oldy ;
		c->w = c->oldw ;
		c->h = c->oldh ;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange(c->mon);
	}
}

void
togglefullscreen(const Arg *arg)
{
	Client *c = selmon->sel ;
	if(c->isfullscreen) setfullscreen(c, 0) ;
	else setfullscreen(c, 1) ;
}

void
setlayout(const Arg *arg)
{
	Monitor *m = selmon ;
	Layout *lt = (Layout *)arg->v ;
	if (!arg || !lt || lt==m->lt ) return ;
	m->lt = lt ;
	m->taglt[m->viewtag].lt = lt-layouts ;
	strncpy( m->ltsymbol, m->lt->symbol, sizeof(m->ltsymbol) );
	arrange(m);
	drawbar(m);
}

unsigned int
nClients(Monitor *mon, uint m)
{
	uint n; Client *c;
	for( n = 0, c = nextclient(mon->clients, m) ; c ;
			c = nextclient(c->next, m), ++n)
		;
	return n ;
}

/* arg > 1.0 will set mfact absolutely. */
void
setmfact(const Arg *arg)
{
	float f;
	Monitor *m = selmon ;
	if(nClients(m, IsVisible|IsTile)<2 || (m->lt == layouts) ) return ;
	if (!arg || !m->lt->arrange) return;
	f = arg->f < 1.0 ?
		arg->f + m->mfact :
			arg->f - 1.0;
	if (f < 0.1 || f > 0.9)
		return;
	m->taglt[m->viewtag].mfact = m->mfact = f;
	arrange(selmon);
}

void
setup(void)
{
	int i;
	Arg arg;
	XSetWindowAttributes wa;
	Atom utf8string;

	/* Clean up any zombies immediately. */
	sigchld(0);

	signal(SIGHUP, sigHup);
	signal(SIGTERM, sigTerm);

	/* Init screen. */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("%s: no fonts could be loaded", argv0);
	lrpad = drw->fonts->h;
	bh = drw->fonts->h + 2;
	updategeom();
	/* Init atoms. */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	/* Init cursors. */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr) ;
	cursor[CurResize] = drw_cur_create(drw, XC_sizing) ;
	cursor[CurMove] = drw_cur_create(drw, XC_fleur) ;
	/* Init appearance. */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *)) ;
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3);
	/* Init bars. */
	updatebars();
	updatestatus();
	/* Supporting window for NetWMCheck. */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) NAME, 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view. */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* Select events. */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	arg.v = &layouts[setup_layouts[0].lt] ;
	setlayout(&arg);
	focus(NULL);
}


void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		/* Show clients top down. */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if ((!c->mon->lt->arrange || c->isfree) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* Hide clients bottom up. */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("%s: can't install SIGCHLD handler", argv0);
	while (0 < waitpid(-1, NULL, WNOHANG));
}

void
spawn(const Arg *arg)
{
	if( arg->v == runcmd ){
		menumon[0] = '0' + selmon->num ;
	}
	if( fork() == 0 ){
		if(dpy){
			close(ConnectionNumber(dpy));
		}
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "%s: execvp %s", argv0, ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

void
tag(const Arg *arg)
{
	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		focus(NULL);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	if (!selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
}

void
scrolldesk(Monitor *m, int dx, int dy, int warpptr)
{
	int x, y;
	if(m->lt != &layouts[LayoutFloating] || nmons > 1){ return ; }
	if( warpptr ){
		/* Move pointer with windows. */
		if( !getrootptr(&x, &y) ) return;
		moveWins(m, dx, dy);
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, x+dx, y+dy);
	}else{
		moveWins(m, dx, dy);
		XRaiseWindow(dpy, m->barwin);
	}
}

void
scrolldeskvertical(const Arg *arg)
{
	scrolldesk(selmon, 0, arg->i, arg->b);
}

void
scrolldeskhorizontal(const Arg *arg)
{
	scrolldesk(selmon, arg->i, 0, arg->b);
}

void
sidehandle(void)
{
	int i, x, y, side;
	if( !getrootptr(&x, &y) ) return;
	side = 0 ;
	if(x==sw-1)
		side |= 1<<SideRight ;
	else if(!x)
		side |= 1<<SideLeft ;
	
	if(y==sh-1)
		side |= 1<<SideDown ;
	else if(!y)
		side |= 1<<SideUp ;

	if(!side)
		side |= 1<<SideNo ;

	for( i = 0 ; i<LENGTH(sides) ; ++i )
		if (side & (1<<sides[i].side) )
			sides[i].func(&(sides[i].arg));
}

void
moveWins(Monitor *m, int dx, int dy)
{
	Client *c;
	for ( c=nextclient(m->clients, IsTile) ; c ; c=nextclient(c->next, IsTile) )
		setFloatingGeometry(c, c->fx + dx, c->fy + dy, c->fw, c->fh);
	arrange(m);
}

void
tile(Monitor *m)
{
	unsigned int i, n, h, mw, my, ty;
	Client *c;

	if ( !(n=nClients(m, IsVisible|IsTile)) ) return ;

	if (n > m->nmaster){
		mw = m->nmaster ? m->ww * m->mfact : 0 ;
	}else{
		mw = m->ww ;
	}
	for (i = my = ty = 0, c = nextclient(m->clients, IsTile|IsVisible);
			c;
			c = nextclient(c->next, IsTile|IsVisible), ++i
	){
		if( i < m->nmaster ){
			h = (m->wh - my) / (MIN(n, m->nmaster) - i) ;
			resize(c,
				m->wx, m->wy + my,
				mw - (2*c->bw), h - (2*c->bw),
				0
			);
			my += HEIGHT(c) ;
		}else{
			h = (m->wh - ty) / (n - i) ;
			resize(c,
				m->wx + mw, m->wy + ty,
				m->ww - mw - (2*c->bw), h - (2*c->bw),
				0
			);
			ty += HEIGHT(c) ;
		}
	}
}

void
rootwin(Monitor *m)
{
	int i, n, nmaster, colw,
		rootx, rooty, rootw, rooth,
		lwinh, rwinh,
		wx, wy, ww, wh;
	float mfact;
	Client *c;

	if ( !(n=nClients(m, IsVisible|IsTile)) ) return ;
	
	nmaster = m->nmaster ; mfact = m->mfact ;
	wx = m->wx ; wy = m->wy ; ww = m->ww ; wh = m->wh ;
	rootx = wx ; rooty = wy ; rootw = ww ; rooth = wh ;

	if(n == 1){ /* Just root win. */
		c = nextclient(m->clients, IsTile|IsVisible) ;
		resize(c, rootx, rooty, rootw, rooth, 0);
		return;
	}
	--n;
	/* First it is space that spare windows will take. */	
	colw = (int)((float)ww * (1-mfact)) ; 
	rootw -= colw ;

	if(nmaster){
		if(nmaster < n ){/* Both. */
			colw /= 2 ;
			rootx += colw ;
			lwinh = wh/nmaster ;
			rwinh = wh/(n-nmaster) ;
		} else { /* Left. */
			rootx += colw ;
			lwinh = wh/n ;
		}
	} else { /* Right. */
		rwinh = wh/n ;
	}
	
	c = nextclient(m->clients, IsTile|IsVisible) ;
	resize(c, rootx, rooty, rootw, rooth, 0);
	
	for(i = 0, c = nextclient(c->next, IsTile|IsVisible) ;
			c;
			c = nextclient(c->next, IsTile|IsVisible), ++i
	){
		if(i < nmaster){ /* Left. */
			resize(c,
				wx + c->bw, wy + c->bw + (lwinh + c->bw*2)*i,
				colw - c->bw*2, lwinh - c->bw*2,
				0
			);
		} else { /* Right. */
			resize(c,
				rootx + rootw + c->bw*2, wy + (rwinh + c->bw*2)*(i-nmaster),
				colw - c->bw, rwinh - c->bw*2,
				0
			);
		}
	}
}

void
split(Monitor* m)
{
	unsigned int i, n, w, mh, mx, tx;
	Client *c;

	if( !(n=nClients(m, IsVisible|IsTile)) ) return ;
	
	if( n> m->nmaster ){
		mh = m->nmaster ? m->wh * m->mfact : 0 ;
	}else{
		mh = m->wh ;
	}
	for( i=mx=tx=0, c=nextclient(m->clients, IsTile|IsVisible) ; c ; c=nextclient(c->next, IsTile|IsVisible), i++){
		if( i < m->nmaster ){
			w = (m->ww - mx) / (MIN(n, m->nmaster) - i) ;
			resize(c,
				m->wx + mx, m->wy,
				w - (2*c->bw),  mh - (2*c->bw),
				0
			);
			mx += WIDTH(c) ;
		}else{
			w = (m->ww-tx) / (n-i) ;
			resize(c,
				m->wx+tx,  m->wy+mh,
				w-(2*c->bw), m->wh-mh-(2*c->bw),
				0
			);
			tx += WIDTH(c) ;
		}
	}
}

void
togglebar(const Arg *arg)
{
	selmon->showbar = !selmon->showbar;
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->by, selmon->ww, bh);
	arrange(selmon);
}

void
togglefree(const Arg *arg)
{
	Monitor *m = selmon ;
	Client *c = selmon->sel ;

	if(! m->sel ) return ; 

	 /* No support for fullscreen windows. */
	if (c->isfullscreen) return ; 

	c->isfree = !c->isfree || c->isfixed ;
	if(c->isfree && m->lt != &layouts[LayoutFloating])
		resize(c, c->freex, c->freey, c->freew, c->freeh, 0);
	arrange(m);
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;

	if (!selmon->sel) { return; }
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK) ;
	if (newtags) {
		selmon->sel->tags = newtags;
		focus(NULL);
		arrange(selmon);
	}
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focus(NULL);
		arrange(selmon);
	}
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	XWindowChanges wc;

	detach(c);
	detachstack(c);
	if (!destroyed) {
		wc.border_width = c->oldbw ;
		XGrabServer(dpy); /* Avoid race conditions. */
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL);
	updateclientlist();
	arrange(m);
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
}

void
updatebars(void)
{
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	} ;
	XClassHint ch = {NAME, NAME} ;
	for (m = mons; m; m = m->next) {
		if (m->barwin)
			continue;
		m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, 0, DefaultDepth(dpy, screen),
				CopyFromParent, DefaultVisual(dpy, screen),
				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
		XMapRaised(dpy, m->barwin);
		XSetClassHint(dpy, m->barwin, &ch);
	}
}

void
updatebarpos(Monitor *m)
{
	m->wy = m->my ;
	m->wh = m->mh ;
	if( m->showbar ){
		m->wh -= bh ;
		m->by = m->topbar ? m->wy : m->wy + m->wh ;
		m->wy = m->topbar ? m->wy + bh : m->wy ;
	}else{
		m->by = -bh ;
	}
}

void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;
		if (n <= nn) { /* new monitors available */
			for (i = 0; i < (nn - n); i++) {
				for (m = mons; m && m->next; m = m->next);
				if (m)
					m->next = createmon();
				else
					mons = createmon();
			}
			for (i = 0, m = mons; i < nn && m; m = m->next, i++)
				if (i >= n
				|| unique[i].x_org != m->mx || unique[i].y_org != m->my
				|| unique[i].width != m->mw || unique[i].height != m->mh)
				{
					dirty = 1;
					m->num = i;
					m->mx = m->wx = unique[i].x_org;
					m->my = m->wy = unique[i].y_org;
					m->mw = m->ww = unique[i].width;
					m->mh = m->wh = unique[i].height;
					updatebarpos(m);
				}
		} else { /* Sess monitors available nn < n . */
			for (i = nn; i < n; i++) {
				for (m = mons; m && m->next; m = m->next);
				while ((c = m->clients)) {
					dirty = 1;
					m->clients = c->next;
					detachstack(c);
					c->mon = mons;
					attach(c);
					attachstack(c);
				}
				if (m == selmon)
					selmon = mons;
				cleanupmon(m);
			}
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* Default monitor setup. */
		if (!mons)
			mons = createmon() ;
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw ;
			mons->mh = mons->wh = sh ;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty ;
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy) ;
	for (i = 0; i < 8; i++){
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
					== XKeysymToKeycode(dpy, XK_Num_Lock)){
				numlockmask = (1 << i) ;
			}
	}
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if(!XGetWMNormalHints(dpy, c->win, &size, &msize)){
		/* Size is uninitialized, ensure that size.flags aren't used. */
		size.flags = PSize ;
	}
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	}else if( size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	}else{
		c->basew = c->baseh = 0 ;
	}
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	}else{
		c->incw = c->inch = 0 ;
	}
	if( size.flags & PMaxSize ){
		c->maxw = size.max_width ;
		c->maxh = size.max_height ;
	}else{
		c->maxw = c->maxh = 0 ;
	}
	if( size.flags & PMinSize ){
		c->minw = size.min_width ;
		c->minh = size.min_height ;
	}else if( size.flags & PBaseSize ){
		c->minw = size.base_width ;
		c->minh = size.base_height ;
	}else{
		c->minw = c->minh = 0;
	}
	if( size.flags & PAspect ){
		c->mina = (float)size.min_aspect.y / size.min_aspect.x ;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y ;
	}else{
		c->maxa = c->mina = 0.0 ;
	}
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh) ;
}

void
updatestatus(void)
{
	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext))){
		strcpy(stext, NAME"-"VERSION);
	}
	drawbar(selmon);
}

void
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name)){
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	}
	if (c->name[0] == '\0'){ /* Hack to mark broken clients. */
		strcpy(c->name, broken);
	}
}

void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen]){
		setfullscreen(c, 1);
	}
	if( wtype == netatom[NetWMWindowTypeDialog] ){
		c->isfree = 1 ;
	}
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		}else{
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0 ;
		}
		if(wmh->flags & InputHint){
			c->neverfocus = !wmh->input;
		}else{
			c->neverfocus = 0;
		}
		XFree(wmh);
	}
}
void
setupview(int nview)
{
	Monitor *m = selmon ;
	m->viewtag = nview ;
	m->mfact = m->taglt[nview].mfact ;
	m->nmaster = m->taglt[nview].nmaster ;
	m->lt = &layouts[ m->taglt[nview].lt ] ;
}

int
bitid(unsigned int bits)
{
	if(!bits)
		return -1 ;
	for( int i=0 ; i<sizeof(int)*8 ; ++i)
		if( (bits>>i) & 1 )
			return i ;
	
	/* Not reachable. */
	return -1 ;
}

void
view(const Arg *arg)
{	
	Monitor *m = selmon ;
	unsigned int mask = arg->ui ;
	unsigned int tagid = bitid(arg->ui) ;

	if((mask & TAGMASK) == m->tagset[selmon->seltags])
		return;

	m->seltags ^= 1 ; /* Toggle sel tagset. */

	if(mask & TAGMASK){
		m->tagset[m->seltags] = mask&TAGMASK ;
		setupview(tagid);
	}
	focus(NULL);
	arrange(selmon);
}

void
viewnext(const Arg *arg)
{
	Monitor *m = selmon ;
	unsigned int viewtag = m->viewtag ;
	
	int i = ((int)viewtag + arg->i)%9 ;
	if(i<0)
		i = 9 + i;
		
	Arg a;
	a.ui =  1<<(i) ;
	view(&a);
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w) return c ;
	return 0 ;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon ;
}

int
waitmouse(XEvent *ev, int type)
{

	if( XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
			None, cursor[CurMove]->cursor, CurrentTime)
			!= GrabSuccess)
		return 1 ;
		
	do{
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, ev);
		switch( ev->type ){
		case ConfigureRequest :
		case Expose :
		case MapRequest :
			handler[ev->type](ev);
		case MotionNotify:
			sidehandle();
		break;
		}
	}while( ev->type != type );

	XUngrabPointer(dpy, CurrentTime) ;

	return 0 ;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
* ignored (especially on UnmapNotify's). Other types of errors call Xlibs
* default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if( ee->error_code == BadWindow
			|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
			|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
			|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
			|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
			|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
			|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
			|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
			|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable) ){
		return 0;
	}
	fprintf(stderr, "%s: fatal error: request code=%d, error code=%d\n",
		argv0, ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* May call exit. */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
* is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("%s: another window manager is already running", argv0);
	return -1;
}

void
zoom(const Arg *arg)
{
	Client *c = selmon->sel;

	if (!selmon->lt->arrange
			|| (selmon->sel && selmon->sel->isfree))
		return;
	if (c == nextclient(selmon->clients, IsTile|IsVisible))
		if (!c || !(c = nextclient(c->next, IsTile|IsVisible)))
			return;
	pop(c);
}

int
main(int argc, char *argv[])
{
	argv0 = argv[0] ;

	if (argc == 2 && !strcmp("-v", argv[1]))
		die(NAME"-"VERSION);
	else if (argc != 1)
		die("Usage: %s [-v]", argv0);

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fprintf(stderr, "%s: warning: no locale support\n", argv0);

	if (!(dpy = XOpenDisplay(NULL)))
		die("%s: cannot open display", argv0);

	checkotherwm();
	setup();

#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("%s: pledge", argv0);
#endif /* __OpenBSD__ */

	scan();
	Arg rcarg = {.v = rccmd} ;
	spawn(&rcarg);
	run();
	if(restart) execvp(argv0, argv) ;
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS ;
}
