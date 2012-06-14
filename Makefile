#See LICENSE for copyright and license details

TARGET 	= srandrd
SOURCE 	= srandrd.c

PREFIX 			?= /usr
INSTALLDIR 	:= $(DESTDIR)$(PREFIX)

MANPREFIX 	?= $(PREFIX)/share/man
MANPREFIX 	:= $(DESTDIR)$(MANPREFIX)

CFLAGS 	:= -Wall -Os -pedantic -Werror -Wextra
LDFLAGS := -lX11 -lXrandr

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS) $(CPPFLAGS)

install: 
	install -d $(INSTALLDIR)/bin
	install -m 755 $(TARGET) $(INSTALLDIR)/bin/
	install -d $(MANPREFIX)/man1
	install -m 644 $(TARGET).1 $(MANPREFIX)/man1

uninstall: 
	$(RM) $(INSTALLDIR)/bin/$(TARGET)
	$(RM) $(MANPREFIX)/man1/$(TARGET).1

clean: 
	$(RM) $(TARGET)

doc: srandrd.1

srandrd.1: srandrd.1.txt 
	a2x --doctype manpage --format manpage $<

.PHONY: all clean install uninstall
