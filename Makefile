# 软件名称：智能环境自适应旋转窗控制系统 V1.0
# 文件名称：Makefile
# 模块说明：主机版严格告警构建与自动化测试
# 对应说明书章节：10 编译与验证

CC = gcc
CPPFLAGS := -Iinclude
CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -Werror -O2 -MMD -MP
LDFLAGS :=
LDLIBS := -lm

BUILD_DIR := build
HOST_BIN := $(BUILD_DIR)/app_host
TEST_BIN := $(BUILD_DIR)/test_host
OBJ_DIR := $(BUILD_DIR)/obj
TEST_OBJ_DIR := $(BUILD_DIR)/test-obj

ifeq ($(OS),Windows_NT)
MKDIR_P = if not exist "$(1)" mkdir "$(1)"
RM_DIR = if exist "$(1)" rmdir /S /Q "$(1)"
else
MKDIR_P = mkdir -p "$(1)"
RM_DIR = rm -rf "$(1)"
endif

COMMON_SOURCES := \
	src/hal_stub.c \
	src/sensor.c \
	src/filter.c \
	src/decision.c \
	src/state_machine.c \
	src/main_window.c \
	src/sub_window.c \
	src/safety.c \
	src/motor.c \
	src/app.c

HOST_OBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(COMMON_SOURCES)) \
	$(OBJ_DIR)/main.o
TEST_OBJECTS := $(patsubst src/%.c,$(TEST_OBJ_DIR)/%.o,$(COMMON_SOURCES)) \
	$(TEST_OBJ_DIR)/test_host.o

.PHONY: all host test clean run

all: host

host: $(HOST_BIN)

test: $(TEST_BIN)
	$(TEST_BIN)

run: $(HOST_BIN)
	$(HOST_BIN)

$(HOST_BIN): $(HOST_OBJECTS)
	@$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_BIN): $(TEST_OBJECTS)
	@$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: src/%.c
	@$(call MKDIR_P,$(OBJ_DIR))
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHOST_BUILD=1 -c $< -o $@

$(OBJ_DIR)/main.o: src/main.c
	@$(call MKDIR_P,$(OBJ_DIR))
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHOST_BUILD=1 -c $< -o $@

$(TEST_OBJ_DIR)/%.o: src/%.c
	@$(call MKDIR_P,$(TEST_OBJ_DIR))
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHOST_BUILD=1 -c $< -o $@

$(TEST_OBJ_DIR)/test_host.o: test/test_host.c
	@$(call MKDIR_P,$(TEST_OBJ_DIR))
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHOST_BUILD=1 -c $< -o $@

clean:
	@$(call RM_DIR,$(BUILD_DIR))

-include $(HOST_OBJECTS:.o=.d)
-include $(TEST_OBJECTS:.o=.d)
