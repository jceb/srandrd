#See LICENSE for copyright and license details

TARGET = srandrd
SOURCE = srandrd.c
PREFIX = /usr

CFLAGS = -Wall -Os -pedantic -Werror -Wextra
LDFLAGS = -lX11 -lXrandr

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

install: 
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall: 
	$(RM) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean: 
	$(RM) $(TARGET)

.PHONY: all clean install uninstall
