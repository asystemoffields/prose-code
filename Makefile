CC      ?= x86_64-w64-mingw32-gcc
WINDRES ?= x86_64-w64-mingw32-windres
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter -municode \
          -DUNICODE -D_UNICODE -DCOBJMACROS
LDFLAGS = -mwindows -municode
LIBS    = -lgdi32 -lcomdlg32 -lcomctl32 -lshell32 -lole32 \
          -lshlwapi -ldwmapi -luxtheme

SRCS    = main.c buffer.c theme.c spell.c syntax.c document.c \
          editor.c search.c menu.c file_io.c render.c wndproc.c
OBJS    = $(SRCS:.c=.o)
TARGET  = prose_code.exe

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c prose_code.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
