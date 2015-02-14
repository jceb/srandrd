#See LICENSE for copyright and license details

TARGET			:= srandrd
SOURCE			:= srandrd.c
VERSION			:= 0.4
COPYRIGHT		:= "(C) 2012-2014 Stefan Bolte"
LICENSE			:= "MIT/X Consortium"

DISTDIR 		:= $(TARGET)-$(VERSION)

PREFIX			?= /usr
INSTALLDIR	:= $(DESTDIR)$(PREFIX)

MANPREFIX		?= $(PREFIX)/share/man
MANPREFIX		:= $(DESTDIR)$(MANPREFIX)

CFLAGS		:= -Wall -Os -pedantic -std=c99 #-Werror -Wextra
CPPFLAGS  += -D_DEFAULT_SOURCE
CPPFLAGS  += -DNAME=\"$(TARGET)\" -DVERSION=\"$(VERSION)\" 
CPPFLAGS	+= -DCOPYRIGHT=\"$(COPYRIGHT)\" -DLICENSE=\"$(LICENSE)\"

LDFLAGS		:= -lX11 -lXrandr

all: $(TARGET)

$(TARGET): $(SOURCE)
	@echo $(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)
	@$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS) $(CPPFLAGS)

install: 
	@echo Installing executable to $(INSTALLDIR)/bin
	@install -d $(INSTALLDIR)/bin
	@install -m 755 $(TARGET) $(INSTALLDIR)/bin/
	@echo Installing manpage to $(MANPREFIX)/man1
	@install -d $(MANPREFIX)/man1
	@install -m 644 $(TARGET).1 $(MANPREFIX)/man1

uninstall: 
	@echo Removing executable from $(INSTALLDIR)/bin
	@rm -f $(INSTALLDIR)/bin/$(TARGET)
	@echo Removing manpage from $(INSTALLDIR)/bin
	@rm -f $(MANPREFIX)/man1/$(TARGET).1

clean: 
	$(RM) $(TARGET)

doc: srandrd.1

srandrd.1: srandrd.1.txt 
	a2x --doctype manpage --format manpage $<

dist: 
	@echo "Creating tarball."
	@hg archive -t tgz -X dist $(DISTDIR).tar.gz

.PHONY: all options clean install uninstall dist
