TARGET=xpomodoro
OFILES=xpomodoro.o
PREFIX=/usr/local

CFLAGS=-Wall -Wextra -std=c89 `pkg-config --cflags x11 xpm` -DUSE_SNDIO
LDFLAGS=`pkg-config --libs x11 xpm xres` -lsndio

all: $(TARGET)
.c.o:
	$(CC) $(CFLAGS) -c $<
$(TARGET): $(OFILES)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OFILES)
clean:
	rm -f $(TARGET) $(OFILES) *.core
install:
	cp $(TARGET) /usr/local/bin/$(TARGET)
	cp xpomodoro.1 /usr/local/man/man1/xpomodoro.1
uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	rm -f $(PREFIX)/man/man1/xpomodoro.1
