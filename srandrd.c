/* See LICENSE for copyright and license details
 *
 * srandrd - simple randr daemon
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

static void 
error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  exit(EXIT_FAILURE);
}
static int 
error_handler() {
  exit(EXIT_SUCCESS);
  return 0;
}
static void 
catch_child(int sig) {
  pid_t pid;
  (void)sig;
  while ( (pid = waitpid(-1, NULL, WNOHANG) > 0));
}
static void 
setup(char **argv) {
  XEvent ev;
  Display *dpy;

  switch(fork()) {
    case -1 : error("Could not fork\n");
    case 0  : break;
    default : exit(EXIT_SUCCESS);
  }

  setsid();

  if (chdir("/") < 0) 
    error("Changing working directory failed");

  if ((dpy = XOpenDisplay(NULL)) == NULL)
    error("Cannot open display\n");

  close(STDIN_FILENO);
  close(STDERR_FILENO);
  close(STDOUT_FILENO);
  signal(SIGCHLD, catch_child);

  XRRSelectInput(dpy, DefaultRootWindow(dpy), RROutputChangeNotifyMask);
  XSync(dpy, False);
  XSetIOErrorHandler((XIOErrorHandler) error_handler);
  while(1) {
    if (!XNextEvent(dpy, &ev)) {
      if (fork() == 0) {
        if (dpy)
          close(ConnectionNumber(dpy));
        setsid();
        execvp(argv[1], &(argv[1]));
      }
    }
  }
}
void 
help(char *name) {
  fprintf(stderr, "Usage: %s command\n", name);
  exit(EXIT_SUCCESS);
}
int 
main(int argc, char **argv) {
  uid_t uid, euid;
  uid = getuid();
  if (argc < 2) 
    help(argv[0]);
  if (uid == 0) 
    error("%s may not run as root\n", argv[0]);
  euid = geteuid();
  if (euid != uid)
    error("Effective uid and uid differ\n", argv[0]);
  setup(argv);
  return 0;
}
