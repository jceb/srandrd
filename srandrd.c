/* See LICENSE for copyright and license details
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
xerror(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  exit(EXIT_FAILURE);
}
static int 
error_handler(void) {
  exit(EXIT_FAILURE);
}
static void 
catch_child(int sig) {
  (void)sig;
  while (waitpid(-1, NULL, WNOHANG) > 0);
}
static void 
help(void) {
  fprintf(stderr, "Usage: "NAME" [option] command\n\n"
      "Options:\n"
      "   -h  Print this help and exit\n" 
      "   -n  Don't fork to background\n" 
      "   -V  Print version information and exit\n");
  exit(EXIT_SUCCESS);
}
static void 
version(void) {
  fprintf(stderr, "    This is : "NAME"\n"
                  "    Version : "VERSION"\n"
                  "  Builddate : "__DATE__" "__TIME__"\n"
                  "  Copyright : "COPYRIGHT"\n"
                  "    License : "LICENSE"\n");
  exit(EXIT_SUCCESS);
}
int 
main(int argc, char **argv) {
  XEvent ev;
  Display *dpy;
  int daemonize = 1, args = 1;
  uid_t uid;

  if (argc < 2) 
    help();
  if (*(argv[1]) == '-') {
    args++; 
    switch(argv[1][1]) {
      case 'V' : version();     
      case 'n' : daemonize = 0; break;
      default : help(); 
    }
  }
  if (argv[args] == NULL)
    help();

  if (((uid = getuid()) == 0) || uid != geteuid()) 
    xerror("%s may not run as root\n", NAME);

  if ((dpy = XOpenDisplay(NULL)) == NULL)
    xerror("Cannot open display\n");

  if (daemonize) {
    switch(fork()) {
      case -1 : xerror("Could not fork\n");
      case 0  : break;
      default : exit(EXIT_SUCCESS);
    }
    setsid();

    close(STDIN_FILENO);
    close(STDERR_FILENO);
    close(STDOUT_FILENO);
  }
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
        execvp(argv[args], &(argv[args]));
      }
    }
  }
  return EXIT_SUCCESS;
}
