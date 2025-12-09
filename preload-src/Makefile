CC = gcc
CFLAGS = -std=c11 -D_GNU_SOURCE -DVERSION=\"0.6.4\" -DPACKAGE=\"preload\" -DPACKAGE_STRING=\"preload-0.6.4\" -DPACKAGE_NAME=\"preload\" -DPACKAGE_BUGREPORT=\"https://github.com/preload-ng\" -DSYSCONFDIR=\"/etc\" -DPKGLOCALSTATEDIR=\"/var/lib/preload\" -DLOGDIR=\"/var/log\" -Wall -Wextra -Wno-unused-parameter -Wno-unused-result
CFLAGS += $(shell pkg-config --cflags glib-2.0)
CFLAGS += -Isrc/core -Isrc/daemon -Isrc/config -Isrc/utils
LDFLAGS = $(shell pkg-config --libs glib-2.0) -lm

# Source files
SRCS = src/core/proc.c src/core/readahead.c src/core/madvise_utils.c src/core/vomm.c src/core/prophet.c src/core/model_utils.c src/core/state.c src/core/context.c \
       src/daemon/preload.c src/daemon/spy.c \
       src/config/conf.c src/config/cmdline.c \
       src/utils/time_utils.c src/utils/log.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

TARGET = preload

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

-include $(DEPS)
