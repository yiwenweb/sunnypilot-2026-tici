/*
 * c3touchd.c - persistent touch injector for comma 3 / sunnypilot
 * Receives "x y down|move|up" lines on unix socket /tmp/c3touch_sock
 * and injects them into the kernel as a virtual touchscreen (uinput).
 * Pure userspace, no system modifications. UAPI: uinput_setup (new style).
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
#define MAX_X 1079
#define MAX_Y 2159
#define MAX_TOUCHES 10

static int ufd = -1;

static void logmsg(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  fflush(stderr);
  va_end(ap);
}

static int set_abs(int fd, __u16 code, int mn, int mx) {
  struct uinput_abs_setup abs;
  memset(&abs, 0, sizeof(abs));
  abs.code = code;
  abs.absinfo.minimum = mn;
  abs.absinfo.maximum = mx;
  if (ioctl(fd, UI_ABS_SETUP, &abs) < 0) {
    logmsg("UI_ABS_SETUP(%u) failed: %s", code, strerror(errno));
    return -1;
  }
  return 0;
}

static int init_uinput(void) {
  ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (ufd < 0) { logmsg("open /dev/uinput: %s", strerror(errno)); return -1; }

  struct uinput_setup setup;
  memset(&setup, 0, sizeof(setup));
  setup.id.bustype = BUS_USB;
  setup.id.vendor = 0x0001;
  setup.id.product = 0x0001;
  setup.id.version = 0x0100;
  strncpy(setup.name, "C3RemoteTouch", UINPUT_MAX_NAME_SIZE - 1);

  if (ioctl(ufd, UI_SET_EVBIT, EV_KEY) < 0) goto fail;
  if (ioctl(ufd, UI_SET_KEYBIT, BTN_TOUCH) < 0) goto fail;
  if (ioctl(ufd, UI_SET_EVBIT, EV_ABS) < 0) goto fail;
  int axes[] = {ABS_X, ABS_Y, ABS_MT_SLOT, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID};
  for (int i = 0; i < 6; i++)
    if (ioctl(ufd, UI_SET_ABSBIT, axes[i]) < 0) goto fail;

  if (ioctl(ufd, UI_DEV_SETUP, &setup) < 0) goto fail;
  if (set_abs(ufd, ABS_X, 0, MAX_X)) return -1;
  if (set_abs(ufd, ABS_Y, 0, MAX_Y)) return -1;
  if (set_abs(ufd, ABS_MT_SLOT, 0, MAX_TOUCHES - 1)) return -1;
  if (set_abs(ufd, ABS_MT_POSITION_X, 0, MAX_X)) return -1;
  if (set_abs(ufd, ABS_MT_POSITION_Y, 0, MAX_Y)) return -1;
  if (set_abs(ufd, ABS_MT_TRACKING_ID, -1, 65535)) return -1;

  if (ioctl(ufd, UI_DEV_CREATE) < 0) goto fail;
  logmsg("uinput device created: C3RemoteTouch");
  return 0;
fail:
  logmsg("uinput init failed: %s", strerror(errno));
  return -1;
}

static void send_ev(__u16 type, __u16 code, __s32 value) {
  struct input_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = type;
  ev.code = code;
  ev.value = value;
  write(ufd, &ev, sizeof(ev));
}

static void sync_frame(void) {
  send_ev(EV_SYN, SYN_REPORT, 0);
}

/* slot 0 single touch */
static void touch_down(int x, int y) {
  send_ev(EV_ABS, ABS_MT_SLOT, 0);
  send_ev(EV_ABS, ABS_MT_TRACKING_ID, 1);
  send_ev(EV_ABS, ABS_MT_POSITION_X, x);
  send_ev(EV_ABS, ABS_MT_POSITION_Y, y);
  send_ev(EV_ABS, ABS_X, x);
  send_ev(EV_ABS, ABS_Y, y);
  send_ev(EV_KEY, BTN_TOUCH, 1);
  sync_frame();
}

static void touch_move(int x, int y) {
  send_ev(EV_ABS, ABS_MT_POSITION_X, x);
  send_ev(EV_ABS, ABS_MT_POSITION_Y, y);
  send_ev(EV_ABS, ABS_X, x);
  send_ev(EV_ABS, ABS_Y, y);
  sync_frame();
}

static void touch_up(void) {
  send_ev(EV_KEY, BTN_TOUCH, 0);
  send_ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
  sync_frame();
}

static void handle_line(char *line) {
  char *save = NULL;
  char *sx = strtok_r(line, " \t\r\n", &save);
  char *sy = strtok_r(NULL, " \t\r\n", &save);
  char *st = strtok_r(NULL, " \t\r\n", &save);
  if (!sx || !sy || !st) return;
  int x = atoi(sx), y = atoi(sy);
  if (x < 0) x = 0;
  if (x > MAX_X) x = MAX_X;
  if (y < 0) y = 0;
  if (y > MAX_Y) y = MAX_Y;

  if (strcmp(st, "down") == 0) touch_down(x, y);
  else if (strcmp(st, "move") == 0) touch_move(x, y);
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
  if (init_uinput() < 0) return 1;

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
  logmsg("c3touchd listening on %s", SOCK_PATH);

  for (;;) {
    int cfd = accept(sfd, NULL, NULL);
    if (cfd < 0) { if (errno == EINTR) continue; logmsg("accept: %s", strerror(errno)); break; }
    pthread_t tid;
    pthread_create(&tid, NULL, client_thread, (void *)(long)cfd);
    pthread_detach(tid);
  }
  close(sfd);
  ioctl(ufd, UI_DEV_DESTROY);
  close(ufd);
  return 0;
}
