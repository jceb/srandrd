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

#define OCNE(X) ((XRROutputChangeNotifyEvent*)X)
#define ACTION_SIZE 128
#define EDID_SIZE 18
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
get_screenid(Display * dpy, RROutput output)
{
	int i, j, screenid = -1;
	XRRScreenResources *sr;
	XRRCrtcInfo *ci;

	sr = XRRGetScreenResourcesCurrent(dpy, DefaultRootWindow(dpy));
	for (i = 0; i < sr->ncrtc; ++i) {
		if ((ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[i]))) {
			for (j = 0; j < ci->noutput; ++j) {
				if (ci->outputs[j] == output) {
					screenid = i;
					break;
				}
			}
			XRRFreeCrtcInfo(ci);
			if (screenid != -1)
				break;
		}
	}
	XRRFreeScreenResources(sr);
	return screenid;
}

int
main(int argc, char **argv)
{
	XEvent ev;
	Display *dpy;
	int daemonize = 1, args = 1, verbose = 0, props = 0, list = 0;
	char action[ACTION_SIZE], edid[EDID_SIZE], screenid[SCREENID_SIZE];
	uid_t uid;
	Atom atom_edid, real;
	Atom *properties;
	Bool has_edid_prop;
	int i, j, format;
	unsigned long n, extra;
	unsigned char *p;
	uint16_t vendor, product;
	uint32_t serial;

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
		XRRScreenResources *sr;
		XRROutputInfo *info;


		sr = XRRGetScreenResourcesCurrent(dpy, DefaultRootWindow(dpy));
		for (i = 0; i < sr->ncrtc; i++) {
			edid[0] = 0;
			info = XRRGetOutputInfo (dpy, sr, sr->outputs[i]);
			printf("%s", info->name);
			XRRFreeOutputInfo(info);

			properties = XRRListOutputProperties(dpy, sr->outputs[i], &props);
			atom_edid = XInternAtom(dpy, RR_PROPERTY_RANDR_EDID, False);
			for (j = 0; j < props; j++) {
				if (properties[j] == atom_edid) {
					has_edid_prop = True;
					break;
				}
			}
			XFree(properties);

			if (has_edid_prop) {
				if (XRRGetOutputProperty(dpy, sr->outputs[i],
							atom_edid, 0L, 128L, False, False, AnyPropertyType, &real, &format,
							&n, &extra, &p) == Success) {
					if (n >= 127) {
						vendor = (p[9] << 8) | p[8];
						product = (p[11] << 8) | p[10];
						serial = p[15] << 24 | p[14] << 16 | p[13] << 8 | p[12];
						snprintf(edid, EDID_SIZE, " %04X%04X%08X", vendor, product, serial);
					}
					free(p);
				}
			}
			printf("%s\n", edid);
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
		vendor = 0;
		product = 0;
		serial = 0;
		edid[0] = 0;
		screenid[0] = 0;
		has_edid_prop = False;

		if (!XNextEvent(dpy, &ev)) {
			XRRScreenResources *resources = XRRGetScreenResources(OCNE(&ev)->display,
																  OCNE(&ev)->window);
			if (resources == NULL) {
				fprintf(stderr, "Could not get screen resources\n");
				continue;
			}

			XRROutputInfo *info = XRRGetOutputInfo(OCNE(&ev)->display, resources,
												   OCNE(&ev)->output);
			if (info == NULL) {
				XRRFreeScreenResources(resources);
				fprintf(stderr, "Could not get output info\n");
				continue;
			}

			properties = XRRListOutputProperties(OCNE(&ev)->display, OCNE(&ev)->output, &props);
			atom_edid = XInternAtom(OCNE(&ev)->display, RR_PROPERTY_RANDR_EDID, False);
			/* atom_edid = XInternAtom(OCNE(&ev)->display, RR_PROPERTY_GUID, False); */
			for (i = 0; i < props; ++i) {
				if (properties[i] == atom_edid) {
					has_edid_prop = True;
					break;
				}
			}
			XFree(properties);
			if (has_edid_prop) {
				if (XRRGetOutputProperty(OCNE(&ev)->display, OCNE(&ev)->output,
										 atom_edid, 0L, 128L, False, False, AnyPropertyType, &real, &format,
										 &n, &extra, &p) == Success) {
					if (n >= 127) {
						vendor = (p[9] << 8) | p[8];
						product = (p[11] << 8) | p[10];
						serial = p[15] << 24 | p[14] << 16 | p[13] << 8 | p[12];
						snprintf(edid, EDID_SIZE, "%04X%04X%08X", vendor, product, serial);
					}
					free(p);
				}
			}

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
					XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, resources, info->crtc);
					if (crtc != NULL) {
						printf("Size: %dx%d\n", crtc->width, crtc->height);
						XRRFreeCrtcInfo(crtc);
					}
				}
				if (vendor) {
					printf("EDID (vendor, product, serial): %04X%04X%08X\n", vendor, product, serial);
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
			XRRFreeScreenResources(resources);
			XRRFreeOutputInfo(info);
		}
	}
	return EXIT_SUCCESS;
}
