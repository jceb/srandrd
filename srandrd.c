/* See LICENSE for copyright and license details
 * srandrd - simple randr daemon
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xinerama.h>

#define OCNE(X) ((XRROutputChangeNotifyEvent*)X)
#define ACTION_SIZE 128
#define EDID_SIZE 17
#define SCREENID_SIZE 3

char *con_actions[] = { "connected", "disconnected", "unknown" };

static void
xerror(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

static int
error_handler(void)
{
	exit(EXIT_FAILURE);
}

static void
catch_child(int sig)
{
	(void) sig;
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void
help(int status)
{
	fprintf(stderr, "Usage: " NAME " [option] command|list\n\n"
			"   list  List outputs and EDIDs and terminate\n\n"
			"Options:\n"
			"   -h  Print this help and exit\n"
			"   -n  Don't fork to background\n"
			"   -V  Print version information and exit\n"
			"   -v  Verbose output\n");
	exit(status);
}

static void
version(void)
{
	fprintf(stderr, "    This is : " NAME "\n"
			"    Version : " VERSION "\n"
			"  Builddate : " __DATE__ " " __TIME__ "\n" "  Copyright : " COPYRIGHT "\n" "    License : " LICENSE "\n");
	exit(EXIT_SUCCESS);
}

int
get_screenid(Display *dpy, RROutput output)
{
	XRRMonitorInfo *mi;
	XRRScreenResources *sr;
	XineramaScreenInfo *si;
	int i, j, k, screenid = -1;
	int nmonitors, nscreens;

	si = XineramaQueryScreens(dpy, &nscreens);
	mi = XRRGetMonitors(dpy, DefaultRootWindow(dpy), True, &nmonitors);
	sr = XRRGetScreenResourcesCurrent(dpy, DefaultRootWindow(dpy));

	for (j = 0; j < nmonitors; ++j) {
		for (k = 0; k < mi[j].noutput && mi[j].outputs[k] != output; ++k);
		if(k < mi[j].noutput)
			break;
	}

	if(si && mi && j < nmonitors) {
		for (i = 0; i < nscreens; ++i) {
				if (si[i].x_org == mi[j].x &&
						si[i].y_org == mi[j].y &&
						si[i].width == mi[j].width &&
						si[i].height == mi[j].height) {
					screenid = i;
					break;
				}
		}
		XFree(si);
		XRRFreeMonitors(mi);
	}
	XRRFreeScreenResources(sr);

	return screenid;
}

int
get_edid(Display *dpy, RROutput out, char *edid, int edidlen)
{
	int props = 0, res = 0;
	Atom *properties;
	Atom atom_edid, real;
	int i, format;
	uint16_t vendor, product;
	uint32_t serial;
	unsigned char *p;
	unsigned long n, extra;
	Bool has_edid_prop = False;

	if(edidlen)
		edid[0] = 0;
	else
		return res;
	properties = XRRListOutputProperties(dpy, out, &props);
	atom_edid = XInternAtom(dpy, RR_PROPERTY_RANDR_EDID, False);
	for (i = 0; i < props; i++) {
		if (properties[i] == atom_edid) {
			has_edid_prop = True;
			break;
		}
	}
	XFree(properties);

	if (has_edid_prop) {
		if (XRRGetOutputProperty(dpy, out, atom_edid, 0L, 128L, False, False,
					AnyPropertyType, &real, &format, &n, &extra, &p) == Success)
		{
			if (n >= 127) {
				vendor = (p[9] << 8) | p[8];
				product = (p[11] << 8) | p[10];
				serial = p[15] << 24 | p[14] << 16 | p[13] << 8 | p[12];
				snprintf(edid, edidlen, "%04X%04X%08X", vendor, product, serial);
				res = EDID_SIZE;
			}
			free(p);
		}
	}
	return res;
}

int
main(int argc, char **argv)
{
	Display *dpy;
	XEvent ev;
	XRRMonitorInfo *mi;
	XRROutputInfo *info;
	XRRScreenResources *sr;
	XineramaScreenInfo *si;
	char action[ACTION_SIZE], edid[EDID_SIZE], screenid[SCREENID_SIZE];
	int daemonize = 1, args = 1, verbose = 0, list = 0;
	int i, j, k, edidlen;
	int nmonitors, nscreens;
	uid_t uid;


	if (argc < 2)
		help(EXIT_FAILURE);

	for (args = 1; args < argc && *(argv[args]) == '-'; args++) {
		switch (argv[args][1]) {
		case 'V':
			version();
		case 'n':
			daemonize = 0;
			break;
		case 'v':
			verbose++;
			break;
		case 'h':
			help(EXIT_SUCCESS);
		default:
			printf("%d\n", list);
			help(EXIT_FAILURE);
		}
	}
	if (argv[args] == NULL)
		help(EXIT_FAILURE);

	if (strncmp("list", argv[args], 5) == 0) {
		list = 1;
	}

	if (((uid = getuid()) == 0) || uid != geteuid())
		xerror("%s may not run as root\n", NAME);

	if ((dpy = XOpenDisplay(NULL)) == NULL)
		xerror("Cannot open display\n");

	if (list) {
		si = XineramaQueryScreens(dpy, &nscreens);
		mi = XRRGetMonitors(dpy, DefaultRootWindow(dpy), True, &nmonitors);
		sr = XRRGetScreenResourcesCurrent(dpy, DefaultRootWindow(dpy));
		if(si && mi) {
			for (i = 0; i < nscreens; ++i) {
				for (j = 0; j < nmonitors; ++j) {
					if (si[i].x_org == mi[j].x &&
							si[i].y_org == mi[j].y &&
							si[i].width == mi[j].width &&
							si[i].height == mi[j].height) {
						for (k = 0; k < mi[j].noutput; ++k) {
							info = XRRGetOutputInfo (dpy, sr, mi[j].outputs[k]);
							get_edid(dpy, mi[j].outputs[k], edid, EDID_SIZE);
							printf("%s %s\n", info->name, edid);
							XRRFreeOutputInfo(info);
						}
						break;
					}
				}
			}
			XFree(si);
			XRRFreeMonitors(mi);
		}
		XRRFreeScreenResources(sr);
		return 0;
	}

	if (daemonize) {
		switch (fork()) {
		case -1:
			xerror("Could not fork\n");
		case 0:
			break;
		default:
			exit(EXIT_SUCCESS);
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
	while (1) {
		screenid[0] = 0;

		if (!XNextEvent(dpy, &ev)) {
			sr = XRRGetScreenResources(OCNE(&ev)->display, OCNE(&ev)->window);
			if (sr == NULL) {
				fprintf(stderr, "Could not get screen resources\n");
				continue;
			}

			info = XRRGetOutputInfo(OCNE(&ev)->display, sr,
												   OCNE(&ev)->output);
			if (info == NULL) {
				XRRFreeScreenResources(sr);
				fprintf(stderr, "Could not get output info\n");
				continue;
			}

			edidlen = get_edid(OCNE(&ev)->display, OCNE(&ev)->output, edid, EDID_SIZE);

			i = get_screenid(OCNE(&ev)->display, OCNE(&ev)->output);
			if (i != -1)
				snprintf(screenid, SCREENID_SIZE, "%d", i);
			snprintf(action, ACTION_SIZE, "%s %s", info->name, con_actions[info->connection]);
			if (verbose) {
				printf("Event: %s %s\n", info->name, con_actions[info->connection]);
				printf("Time: %lu\n", info->timestamp);
				if (info->crtc == 0) {
					printf("Size: %lumm x %lumm\n", info->mm_width, info->mm_height);
				}
				else {
					printf("CRTC: %lu\n", info->crtc);
					XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, sr, info->crtc);
					if (crtc != NULL) {
						printf("Size: %dx%d\n", crtc->width, crtc->height);
						XRRFreeCrtcInfo(crtc);
					}
				}
				if (edidlen) {
					printf("EDID (vendor, product, serial): %s\n", edid);
				}
				else {
					printf("EDID (vendor, product, serial): not available\n");
				}
			}
			if (fork() == 0) {
				if (dpy)
					close(ConnectionNumber(dpy));
				setsid();
				setenv("SRANDRD_ACTION", action, False);
				setenv("SRANDRD_EDID", edid, False);
				setenv("SRANDRD_SCREENID", screenid, False);
				execvp(argv[args], &(argv[args]));
			}
			XRRFreeScreenResources(sr);
			XRRFreeOutputInfo(info);
		}
	}
	return EXIT_SUCCESS;
}
