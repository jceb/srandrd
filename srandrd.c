/* See LICENSE for copyright and license details
 * srandrd - simple randr daemon
 */
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xinerama.h>

#define OCNE(X) ((XRROutputChangeNotifyEvent*)X)
#define EVENT_SIZE 128
#define EDID_SIZE 17
#define SCREENID_SIZE 3

typedef struct OutputConnection OutputConnection;

struct OutputConnection
{
	RROutput output;
	int sid;
	char *edid;
	int edidlen;
	OutputConnection *next;
};

char *CON_EVENTS[] = { "connected", "disconnected", "unknown" };

OutputConnection *CONNECTIONS = 0;

char **ARGV;
int ARGS;

OutputConnection *cache_connection(OutputConnection * head, RROutput output, char *edid, int edidlen, int sid);
static void xerror(const char *format, ...);
static int error_handler(void);
static void catch_child(int sig);
static void help(int status);
static void version(void);
int get_screenid(Display * dpy, RROutput output);
int get_edid(Display * dpy, RROutput out, char *edid, int edidlen);
int iter_crtcs(Display * dpy, void (*f) (Display *, char *, char *, int));
void print_crtc(Display * dpy, char *name, char *edid, int sid);
pid_t emit(Display * dpy, char *output, char *event, char *edid, char *screenid);
void emit_crtc(Display * dpy, char *name, char *edid, int sid);
void free_output_connection(OutputConnection * ocon);
OutputConnection * get_output_connection(OutputConnection * ocon, RROutput output);
OutputConnection * remove_output_connection(OutputConnection * ocon, RROutput output);
OutputConnection * add_output_connection(OutputConnection * head, OutputConnection * ocon);
void die_if_null(void *ptr);
OutputConnection * cache_connection(OutputConnection * head, RROutput output, char *edid, int edidlen, int sid);
int process_events(Display * dpy, int verbose);
int main(int argc, char **argv);

void
die_if_null(void *ptr)
{
	if (!ptr) {
		fprintf(stderr, "Malloc failed.");
		error_handler();
	}
}

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
	while (waitpid(-1, NULL, WNOHANG) > 0) {
		;
	}
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
			"   -v  Verbose output\n"
			"   -e  Emit connected devices at startup\n"
			"   -1  One-shot mode (emit devices and exit)\n");
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
get_sid(Display * dpy, RROutput output)
{
	XRRMonitorInfo *mi;
	XRRScreenResources *sr;
	XineramaScreenInfo *si;
	int i, j, k, sid = -1;
	int nmonitors, nscreens;

	si = XineramaQueryScreens(dpy, &nscreens);
	mi = XRRGetMonitors(dpy, DefaultRootWindow(dpy), True, &nmonitors);
	sr = XRRGetScreenResourcesCurrent(dpy, DefaultRootWindow(dpy));

	for (j = 0; j < nmonitors; ++j) {
		for (k = 0; k < mi[j].noutput && mi[j].outputs[k] != output; ++k) {
			;
		}
		if (k < mi[j].noutput) {
			break;
		}
	}

	if (si && mi && j < nmonitors) {
		for (i = 0; i < nscreens; ++i) {
			if (si[i].x_org == mi[j].x &&
				si[i].y_org == mi[j].y && si[i].width == mi[j].width && si[i].height == mi[j].height) {
				sid = i;
				break;
			}
		}
		XFree(si);
		XRRFreeMonitors(mi);
	}
	XRRFreeScreenResources(sr);

	return sid;
}

int
get_edid(Display * dpy, RROutput out, char *edid, int edidlen)
{
	int props = 0, len = 0;
	Atom *properties;
	Atom atom_edid, real;
	int i, format;
	uint16_t vendor, product;
	uint32_t serial;
	unsigned char *p;
	unsigned long n, extra;
	Bool has_edid_prop = False;

	if (edidlen) {
		edid[0] = 0;
	}
	else {
		return len;
	}
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
								 AnyPropertyType, &real, &format, &n, &extra, &p) == Success) {
			if (n >= 127) {
				vendor = (p[9] << 8) | p[8];
				product = (p[11] << 8) | p[10];
				serial = p[15] << 24 | p[14] << 16 | p[13] << 8 | p[12];
				snprintf(edid, edidlen, "%04X%04X%08X", vendor, product, serial);
				len = EDID_SIZE;
			}
			free(p);
		}
	}
	return len;
}

int
iter_crtcs(Display * dpy, void (*f) (Display *, char *, char *, int))
{
	XRRMonitorInfo *mi;
	XRRScreenResources *sr;
	XineramaScreenInfo *si;
	XRROutputInfo *info;
	char edid[EDID_SIZE];
	int i, j, k, edidlen, nmonitors, nscreens, sid = -1;

	si = XineramaQueryScreens(dpy, &nscreens);
	mi = XRRGetMonitors(dpy, DefaultRootWindow(dpy), True, &nmonitors);
	sr = XRRGetScreenResourcesCurrent(dpy, DefaultRootWindow(dpy));
	if (si && mi) {
		for (i = 0; i < nscreens; ++i) {
			for (j = 0; j < nmonitors; ++j) {
				for (k = 0; k < mi[j].noutput; ++k) {
					sid = get_sid(dpy, mi[j].outputs[k]);
					if (i != sid) {
						continue;
					}
					info = XRRGetOutputInfo(dpy, sr, mi[j].outputs[k]);
					edidlen = get_edid(dpy, mi[j].outputs[k], edid, EDID_SIZE);
					CONNECTIONS = cache_connection(CONNECTIONS, mi[j].outputs[k], edid, edidlen, sid);
					f(dpy, info->name, edid, sid);
					XRRFreeOutputInfo(info);
				}
				/* INFO: apparently this causes a number of displays to not be
				 * listed, see https://github.com/jceb/srandrd/issues/10 */
				// if (i == sid) {
				// 	break;
				// }
			}
		}
		XFree(si);
		XRRFreeMonitors(mi);
	}
	XRRFreeScreenResources(sr);
	return EXIT_SUCCESS;
}

void
print_crtc(Display * dpy, char *name, char *edid, int sid)
{
	printf("%s %s\n", name, edid);
}

pid_t
emit(Display * dpy, char *output, char *event, char *edid, char *screenid)
{
	pid_t pid;
	if ((pid = fork()) == 0) {
		if (dpy) {
			close(ConnectionNumber(dpy));
		}
		setsid();
		setenv("SRANDRD_OUTPUT", output, True);
		setenv("SRANDRD_EVENT", event, True);
		setenv("SRANDRD_EDID", edid, True);
		setenv("SRANDRD_SCREENID", screenid, True);
		execvp(ARGV[ARGS], &(ARGV[ARGS]));
	}
	return waitpid(pid, NULL, 0);
}

void
emit_crtc(Display * dpy, char *name, char *edid, int sid)
{
	char screenid[SCREENID_SIZE];
	screenid[0] = 0;
	if (sid != -1) {
		snprintf(screenid, SCREENID_SIZE, "%d", sid);
	}
	emit(dpy, name, CON_EVENTS[0], edid, screenid);
}

void
free_output_connection(OutputConnection * ocon)
{
	if (ocon) {
		free(ocon->edid);
		free(ocon);
	}
}

OutputConnection *
get_output_connection(OutputConnection * ocon, RROutput output)
{
	OutputConnection *_ocon = ocon;
	for (; _ocon && _ocon->output != output; _ocon = _ocon->next);
	return _ocon;
}

OutputConnection *
remove_output_connection(OutputConnection * ocon, RROutput output)
{
	OutputConnection *_ocon = ocon;
	if (_ocon && _ocon->output == output) {
		OutputConnection *next = _ocon->next;
		free_output_connection(_ocon);
		return next;
	}
	for (; _ocon && _ocon->next && _ocon->next->output != output; _ocon = _ocon->next);
	if (_ocon && _ocon->next && _ocon->next->output == output) {
		OutputConnection *old_ocon = _ocon->next;
		_ocon->next = _ocon->next->next;
		free_output_connection(old_ocon);
	}
	return ocon;
}

OutputConnection *
add_output_connection(OutputConnection * head, OutputConnection * ocon)
{
	OutputConnection *_ocon = head;
	if (!ocon) {
		return head;
	}
	if (!head) {
		return ocon;
	}
	for (; _ocon && _ocon->next; _ocon = _ocon->next);
	_ocon->next = ocon;
	return head;
}

OutputConnection *
cache_connection(OutputConnection * head, RROutput output, char *edid, int edidlen, int sid)
{
	OutputConnection *ocon = malloc(sizeof(OutputConnection));
	die_if_null(ocon);
	memset(ocon, 0, sizeof(OutputConnection));
	ocon->output = output;

	ocon->edid = malloc(sizeof(char) * EDID_SIZE);
	memset(ocon->edid, 0, EDID_SIZE);
	die_if_null(ocon->edid);

	ocon->edidlen = edidlen;
	strncpy(ocon->edid, edid, edidlen);

	ocon->sid = sid;

	return add_output_connection(head, ocon);
}

int
process_events(Display * dpy, int verbose)
{
	XRRScreenResources *sr;
	XRROutputInfo *info;
	XEvent ev;
	char edid[EDID_SIZE], screenid[SCREENID_SIZE];
	int i, edidlen;

	XRRSelectInput(dpy, DefaultRootWindow(dpy), RROutputChangeNotifyMask);
	XSync(dpy, False);
	XSetIOErrorHandler((XIOErrorHandler) error_handler);
	while (1) {
		if (!XNextEvent(dpy, &ev)) {
			i = -1;
			edidlen = 0;
			memset(edid, 0, EDID_SIZE);
			memset(screenid, 0, SCREENID_SIZE);

			sr = XRRGetScreenResources(OCNE(&ev)->display, OCNE(&ev)->window);
			if (sr == NULL) {
				fprintf(stderr, "Could not get screen resources\n");
				continue;
			}

			info = XRRGetOutputInfo(OCNE(&ev)->display, sr, OCNE(&ev)->output);
			if (info == NULL) {
				XRRFreeScreenResources(sr);
				fprintf(stderr, "Could not get output info\n");
				continue;
			}

			if (strncmp(CON_EVENTS[info->connection], "disconnected", 12) == 0) {
				/* retrieve edid and screen information from cache */
				OutputConnection *ocon = get_output_connection(CONNECTIONS, OCNE(&ev)->output);
				if (ocon) {
					i = ocon->sid;
					edidlen = ocon->edidlen;
					strncpy(edid, ocon->edid, edidlen);
					CONNECTIONS = remove_output_connection(CONNECTIONS, OCNE(&ev)->output);
				}
			}
			else {
				edidlen = get_edid(OCNE(&ev)->display, OCNE(&ev)->output, edid, EDID_SIZE);
				i = get_sid(OCNE(&ev)->display, OCNE(&ev)->output);
				CONNECTIONS = cache_connection(CONNECTIONS, OCNE(&ev)->output, edid, edidlen, i);
			}
			if (i != -1) {
				snprintf(screenid, SCREENID_SIZE, "%d", i);
			}

			if (verbose) {
				printf("Event: %s %s\n", info->name, CON_EVENTS[info->connection]);
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
			emit(dpy, info->name, CON_EVENTS[info->connection], edid, screenid);
			XRRFreeScreenResources(sr);
			XRRFreeOutputInfo(info);
		}
	}
	return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
	Display *dpy;
	int daemonize = 1, args = 1, verbose = 0, emit = 0, list = 0, oneshot = 0;
	int rv = 0;
	uid_t uid;

	if (argc < 2) {
		help(EXIT_FAILURE);
	}

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
		case '1':
			oneshot++;
			/* fallthrough: oneshot implies emit */
		case 'e':
			emit++;
			break;
		case 'h':
			help(EXIT_SUCCESS);
		default:
			printf("%d\n", list);
			help(EXIT_FAILURE);
		}
	}
	if (argv[args] == NULL) {
		help(EXIT_FAILURE);
	}

	if (strncmp("list", argv[args], 5) == 0) {
		list = 1;
	}

	if (((uid = getuid()) == 0) || uid != geteuid()) {
		xerror("This program may not run as root\n");
	}

	if ((dpy = XOpenDisplay(NULL)) == NULL) {
		xerror("Cannot open display\n");
	}

	if (list) {
		return iter_crtcs(dpy, &print_crtc);
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

	ARGV = argv;
	ARGS = args;

	if (emit)
		rv = iter_crtcs(dpy, &emit_crtc);
	if (!oneshot)
		rv = process_events(dpy, verbose);
	return rv;
}

/* vi: ft=c:tw=80:sw=4:ts=4:sts=4:noet
 */
