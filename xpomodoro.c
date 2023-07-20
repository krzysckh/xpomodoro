#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <err.h>

#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <X11/Xutil.h>

#include <sys/mman.h>

#ifdef USE_SNDIO
#define USE_SOUND
#include <sndio.h>
#endif

#include "tomato.xpm"

#define WIN_WIDTH 300
#define WIN_HEIGHT 50

#define WORK_TEXT   "pomodoro cooking up..."
#define BREAK_TEXT  "time for a break!"
#define WORK_COLOR  0xFFFF0000
#define BREAK_COLOR 0xFF00FF00
#define WORK_TIME   (25 * 60)
#define BREAK_TIME  (5  * 60)

typedef enum {
  Work  = 0,
  Break = 1
} State;

static const unsigned long BACKGROUND_COLOR = 0xFF4d4d4d;
static const unsigned long FOREGROUND_COLOR = 0xFFFFFFFF;
static char *clock_text;
static int *refresh;
static State *state;

#ifdef USE_SNDIO
/* https://github.com/zavok/beep */
int mkblock(struct sio_par *par, char **bp) {
	int i, length, bsize;
	char *nb;

	length = par->rate / 440;
	bsize = length * par->pchan * par->bps;

	nb = malloc(bsize);
	for (i = 0; i < length; i += par->pchan * par->bps)
		nb[i] = i > length / 2 ? 0: 0xff;

	*bp = nb;
	return bsize;
}

static void beep(void) {
  struct sio_hdl *snd = NULL;
  struct sio_par par;
  int sz, i;
  char *b;

  snd = sio_open(SIO_DEVANY, SIO_PLAY, 0);
  if (!snd) {
    warnx("couldn't open audio device");
    return;
  }

  sio_initpar(&par);
  par.bits = 8;
  par.bps = 1;
  par.sig = 0;
  par.le = 1;
  par.pchan = 1;
  par.rate = 44100;
  par.xrun = SIO_IGNORE;

  if (!sio_setpar(snd, &par)) {
    warnx("can't sio_par()");
    return;
  }

  if (!sio_getpar(snd, &par)) {
    warnx("can't soi_getpar()");
    return;
  }

  if (!sio_start(snd)) {
    warnx("can't sio_start()");
    return;
  }

  sz = mkblock(&par, &b);
  if (sz <= 0)
    warnx("couldn't beep()");


  for (i = 0; i < 50; i++)
    sio_write(snd, b, sz);

  sio_stop(snd);
  sio_close(snd);
  free(b);
}
#endif /* USE_SNDIO */

static void run_clock(void) {
  int timer = WORK_TIME;

  if (fork() == 0) {
    while (1) {
      timer = timer - 1;

      snprintf(clock_text, 64, "%02d:%02d", (int)(timer / 60),
        timer - (60 * ((int)(timer / 60)))); /* lmao */

      *refresh = 1;
      sleep(1);

      if (timer <= 0) {
#ifdef USE_SOUND
        beep();
#endif
        switch (*state) {
          case Work:
            timer = BREAK_TIME;
            *state = Break;
            break;
          case Break:
            timer = WORK_TIME;
            *state = Work;
            break;
        }
      }
    }
  }
}

static void alloc_glob(void) {
  clock_text = mmap(NULL, 64, PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  refresh = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  state = mmap(NULL, sizeof(State), PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  memset(clock_text, 0, 64);
}

static void free_glob(void) {
  munmap(clock_text, 64);
  munmap(refresh, sizeof(int));
  munmap(state, sizeof(State));
}

int main (void) {
  Display *dpy;
  Window w;
  XSizeHints sz_hints;
  XEvent ev;
  XImage *tomato, *tomato_shape;
  GC gc, gc_green, gc_red;
  XGCValues gcv;
  int run = 1;

  alloc_glob();

  XInitThreads();

  dpy = XOpenDisplay(NULL);
  if (!dpy)
    errx(1, "couldn't open display");

  XpmCreateImageFromData(dpy, (char**)tomato_xpm, &tomato, &tomato_shape, NULL);

  w = XCreateSimpleWindow(dpy, XDefaultRootWindow(dpy), 0, 0, WIN_WIDTH,
      WIN_HEIGHT, 0, 0, BACKGROUND_COLOR);

  gc = DefaultGC(dpy, 0);
  XSetForeground(dpy, gc, FOREGROUND_COLOR);

  gcv.foreground = BREAK_COLOR;
  gc_green = XCreateGC(dpy, w, GCForeground, &gcv);
  gcv.foreground = WORK_COLOR;
  gc_red = XCreateGC(dpy, w, GCForeground, &gcv);

  sz_hints.flags = PBaseSize | PMinSize | PMaxSize | PSize;
  sz_hints.base_width = sz_hints.width = sz_hints.max_width = sz_hints.min_width
    = WIN_WIDTH;
  sz_hints.base_height = sz_hints.height = sz_hints.max_height =
    sz_hints.min_height = WIN_HEIGHT;
  /* this is very ghetto */

  XSetWMNormalHints(dpy, w, &sz_hints);

  XSelectInput(dpy, w, KeyPressMask | ExposureMask);

  XMapWindow(dpy, w);
  XSync(dpy, 0);

  run_clock();

  while (run) {
    if (XPending(dpy) > 0) {
      XNextEvent(dpy, &ev);

      switch (ev.type) {
        case Expose:
          /* redraw */
          XPutImage(dpy, w, gc, tomato, 0, 0, 0, 0, 53, 50);
          XDrawString(dpy, w, gc, 64, 10, clock_text,
            strlen(clock_text));

          switch (*state) {
            case Break:
              XDrawString(dpy, w, gc_green, 64, 32, BREAK_TEXT,
                strlen(BREAK_TEXT));
              break;
            case Work:
              XDrawString(dpy, w, gc_red, 64, 32, WORK_TEXT,
                strlen(WORK_TEXT));
              break;
            default:
              errx(1, "unreachable");
          }
          break;
      }
    }

    ev.type = Expose;
    if (*refresh) {
      XClearArea(dpy, w, 53, 0, WIN_WIDTH - 53, WIN_HEIGHT, 1);
      *refresh = 0;
    }
    /* it is _a_ solution. not _a good_ solution */
  }

  XCloseDisplay(dpy);
  free_glob();
}
