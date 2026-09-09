/*
 * c3touchd.c - touch injector for comma 3 / sunnypilot
 *
 * Receives "x y down|move|up" lines on unix socket /tmp/c3touch_sock
 * where (x,y) are UI LANDSCAPE coordinates (x: 0..2159, y: 0..1079,
 * matching the 2160x1080 UI render), and injects them into the REAL
 * touchscreen device node /dev/input/event2.
 *
 * WHY event2 (not uinput): the raylib-based UI is built with
 * PLATFORM_DRM and HARDCODES "/dev/input/event2" (the Samsung
 * Touchscreen). It reads that node directly and never sees events
 * on a uinput virtual device (event3). Writing input_event structs
 * to /dev/input/event2 (mode 666) is pure userspace and the kernel
 * fans events out to every reader (UI + weston), exactly like a
 * real finger. Verified working.
 *
 * Coordinate mapping (verified on device):
 *   UI landscape point (ux, uy), ux in [0,2159], uy in [0,1079]
 *   device portrait node: ABS_X in [0,1080], ABS_Y in [0,2160]
 *   dev_x = 1080 - uy
 *   dev_y = ux
 *
 * Pure userspace, no system modifications.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define SOCK_PATH "/tmp/c3touch_sock"

/* UI landscape coordinate space (2160x1080) */
#define UI_MAX_X 2159
#define UI_MAX_Y 1079

/* Real touchscreen device node - raylib UI reads this HARDCODED node */
#define TOUCH_DEV "/dev/input/event2"
/* Physical device ABS ranges (portrait 1080x2160) */
#define DEV_MAX_X 1080
#define DEV_MAX_Y 2160

static int tfd = -1;

static void logmsg(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  fflush(stderr);
  va_end(ap);
}

static int init_touchdev(void) {
  tfd = open(TOUCH_DEV, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
  if (tfd < 0) {
    logmsg("open %s (O_WRONLY): %s", TOUCH_DEV, strerror(errno));
    return -1;
  }
  logmsg("opened %s for injection", TOUCH_DEV);
  return 0;
}

static void send_ev(__u16 type, __u16 code, __s32 value) {
  struct input_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = type;
  ev.code = code;
  ev.value = value;
  ssize_t n = write(tfd, &ev, sizeof(ev));
  if (n != (ssize_t)sizeof(ev)) {
    /* EAGAIN on nonblocking is possible under load; retry once blocking */
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      int fl = fcntl(tfd, F_GETFL, 0);
      fcntl(tfd, F_SETFL, fl & ~O_NONBLOCK);
      write(tfd, &ev, sizeof(ev));
      fcntl(tfd, F_SETFL, fl);
    }
  }
}

static void sync_frame(void) {
  send_ev(EV_SYN, SYN_REPORT, 0);
}

/* Transform UI landscape (ux, uy) -> device portrait (dx, dy).
 * Verified on device: dev_x = 1080 - uy, dev_y = ux (90 CW + flip). */
static void transform(int ux, int uy, int *dx, int *dy) {
  *dx = DEV_MAX_X - uy;
  *dy = ux;
}

/* Touch tracking id - must stay stable across a down..move..up cycle */
#define TRACKING_ID 42

static void touch_down(int dx, int dy) {
  send_ev(EV_ABS, ABS_MT_SLOT, 0);
  send_ev(EV_ABS, ABS_MT_TRACKING_ID, TRACKING_ID);
  send_ev(EV_ABS, ABS_MT_POSITION_X, dx);
  send_ev(EV_ABS, ABS_MT_POSITION_Y, dy);
  send_ev(EV_ABS, ABS_MT_PRESSURE, 64);
  send_ev(EV_ABS, ABS_X, dx);
  send_ev(EV_ABS, ABS_Y, dy);
  send_ev(EV_KEY, BTN_TOUCH, 1);
  sync_frame();
}

static void touch_move(int dx, int dy) {
  send_ev(EV_ABS, ABS_MT_SLOT, 0);
  send_ev(EV_ABS, ABS_MT_POSITION_X, dx);
  send_ev(EV_ABS, ABS_MT_POSITION_Y, dy);
  send_ev(EV_ABS, ABS_MT_PRESSURE, 64);
  send_ev(EV_ABS, ABS_X, dx);
  send_ev(EV_ABS, ABS_Y, dy);
  sync_frame();
}

static void touch_up(void) {
  send_ev(EV_ABS, ABS_MT_SLOT, 0);
  send_ev(EV_KEY, BTN_TOUCH, 0);
  send_ev(EV_ABS, ABS_MT_PRESSURE, 0);
  send_ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
  sync_frame();
}

static void handle_line(char *line) {
  char *save = NULL;
  char *sx = strtok_r(line, " \t\r\n", &save);
  char *sy = strtok_r(NULL, " \t\r\n", &save);
  char *st = strtok_r(NULL, " \t\r\n", &save);
  if (!sx || !sy || !st) return;
  int ux = atoi(sx), uy = atoi(sy);
  if (ux < 0) ux = 0;
  if (ux > UI_MAX_X) ux = UI_MAX_X;
  if (uy < 0) uy = 0;
  if (uy > UI_MAX_Y) uy = UI_MAX_Y;

  int dx, dy;
  transform(ux, uy, &dx, &dy);

  if (strcmp(st, "down") == 0) touch_down(dx, dy);
  else if (strcmp(st, "move") == 0) touch_move(dx, dy);
  else if (strcmp(st, "up") == 0) touch_up();
}

static void *client_thread(void *arg) {
  int cfd = (int)(long)arg;
  FILE *fp = fdopen(cfd, "r");
  if (!fp) { close(cfd); return NULL; }
  char buf[128];
  while (fgets(buf, sizeof(buf), fp)) {
    handle_line(buf);
  }
  fclose(fp);
  return NULL;
}

int main(void) {
  if (init_touchdev() < 0) return 1;

  unlink(SOCK_PATH);
  int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sfd < 0) { logmsg("socket: %s", strerror(errno)); return 1; }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);
  if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    logmsg("bind: %s", strerror(errno)); return 1;
  }
  chmod(SOCK_PATH, 0777);
  if (listen(sfd, 8) < 0) { logmsg("listen: %s", strerror(errno)); return 1; }
  logmsg("c3touchd listening on %s (inject -> %s, UI-landscape coords)", SOCK_PATH, TOUCH_DEV);

  for (;;) {
    int cfd = accept(sfd, NULL, NULL);
    if (cfd < 0) { if (errno == EINTR) continue; logmsg("accept: %s", strerror(errno)); break; }
    pthread_t tid;
    pthread_create(&tid, NULL, client_thread, (void *)(long)cfd);
    pthread_detach(tid);
  }
  close(sfd);
  close(tfd);
  return 0;
}
