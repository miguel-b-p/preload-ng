CC = gcc
CFLAGS = -std=c11 -D_GNU_SOURCE -DVERSION=\"0.6.4\" -DPACKAGE=\"preload\" -DPACKAGE_STRING=\"preload-0.6.4\" -DPACKAGE_NAME=\"preload\" -DPACKAGE_BUGREPORT=\"https://github.com/preload-ng\" -DSYSCONFDIR=\"/etc\" -DPKGLOCALSTATEDIR=\"/var/lib/preload\" -DLOGDIR=\"/var/log\" -Wall -Wextra -Wno-unused-parameter -Wno-unused-result
CFLAGS += $(shell pkg-config --cflags glib-2.0)
# 3-pillar structure: monitoring, handling, algorithm
CFLAGS += -Isrc/monitoring -Isrc/handling -Isrc/algorithm -Isrc/config -Isrc/daemon -Isrc/utils
LDFLAGS = $(shell pkg-config --libs glib-2.0) -lm

# Source files organized by pillar
MONITORING_SRCS = src/monitoring/proc.c src/monitoring/spy.c
HANDLING_SRCS = src/handling/state.c src/handling/state_io.c src/handling/map.c src/handling/exe.c \
                src/handling/readahead.c src/handling/madvise_utils.c src/handling/model_utils.c \
                src/handling/context.c
ALGORITHM_SRCS = src/algorithm/markov.c src/algorithm/vomm.c src/algorithm/prophet.c
CONFIG_SRCS = src/config/conf.c src/config/cmdline.c
DAEMON_SRCS = src/daemon/preload.c
UTILS_SRCS = src/utils/time_utils.c src/utils/log.c src/utils/power.c

SRCS = $(MONITORING_SRCS) $(HANDLING_SRCS) $(ALGORITHM_SRCS) $(CONFIG_SRCS) $(DAEMON_SRCS) $(UTILS_SRCS)
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

TARGET = preload

# Test files
TEST_SRCS = src/tests/test_main.c src/tests/test_markov.c src/tests/test_vomm.c src/tests/test_state_io.c \
            src/tests/test_exe.c src/tests/test_map.c src/tests/test_model_utils.c src/tests/test_time_utils.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_DEPS = $(TEST_SRCS:.c=.d)
# Core objects needed for tests (exclude daemon entry point)
TEST_CORE_OBJS = $(filter-out src/daemon/preload.o,$(OBJS))
TEST_TARGET = test_runner

.PHONY: all clean test

all: test $(TARGET)
	chmod +x post_build/post_build
	./post_build/post_build

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Test target
test: $(TEST_TARGET)
	@echo ""
	@echo "Running tests..."
	@./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS) $(TEST_CORE_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET) $(TARGET).conf $(TARGET).state $(TARGET).log
	rm -f $(TEST_OBJS) $(TEST_TARGET)
	rm -f src/tests/*.d

-include $(DEPS) $(TEST_DEPS)
