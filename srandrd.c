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
#define BUFFER_SIZE 128

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
	fprintf(stderr, "Usage: " NAME " [option] command\n\n"
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
main(int argc, char **argv)
{
	XEvent ev;
	Display *dpy;
	int daemonize = 1, args = 1, verbose = 0, props = 0;
	char buf[BUFFER_SIZE], edid[17];
	uid_t uid;
	Atom atom_edid, real;
	Atom *properties;
	Bool has_edid_prop;
	int format;
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
			help(EXIT_FAILURE);
		}
	}
	if (argv[args] == NULL)
		help(EXIT_FAILURE);

	if (((uid = getuid()) == 0) || uid != geteuid())
		xerror("%s may not run as root\n", NAME);

	if ((dpy = XOpenDisplay(NULL)) == NULL)
		xerror("Cannot open display\n");

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
			for (int i = 0; i < props; ++i) {
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
						snprintf(edid, 17, "%04X%04X%08X", vendor, product, serial);
					}
					XFree(p);
				}
			}

			snprintf(buf, BUFFER_SIZE, "%s %s", info->name, con_actions[info->connection]);
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
				} else {
					printf("EDID (vendor, product, serial): not available\n");
				}
			}
			if (fork() == 0) {
				if (dpy)
					close(ConnectionNumber(dpy));
				setsid();
				setenv("SRANDRD_ACTION", buf, False);
				setenv("SRANDRD_EDID", edid, False);
				execvp(argv[args], &(argv[args]));
			}
			XRRFreeScreenResources(resources);
			XRRFreeOutputInfo(info);
		}
	}
	return EXIT_SUCCESS;
}
