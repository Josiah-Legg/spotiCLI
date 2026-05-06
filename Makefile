CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2 -D_WIN32_WINNT=0x0601
LDFLAGS = -lm

ifeq ($(OS),Windows_NT)
LDFLAGS += -lwinhttp -lws2_32 -lshlwapi -lshell32 -lole32
EXE = .exe
RM = rm -rf
MKDIR = mkdir -p
else
EXE =
RM = rm -rf
MKDIR = mkdir -p
endif

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = .

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/state.c \
          $(SRC_DIR)/auth.c \
          $(SRC_DIR)/api.c \
          $(SRC_DIR)/http.c \
          $(SRC_DIR)/json_util.c \
          $(SRC_DIR)/tui.c \
          $(SRC_DIR)/album_art.c \
          $(SRC_DIR)/lyrics.c \
          $(SRC_DIR)/modes.c \
          $(SRC_DIR)/queue_widget.c \
          $(SRC_DIR)/search.c \
          $(SRC_DIR)/settings.c \
          $(SRC_DIR)/input.c \
          $(SRC_DIR)/player.c

HEADERS = $(wildcard $(SRC_DIR)/*.h)

OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

TARGET = $(BIN_DIR)/spoticli$(EXE)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@$(MKDIR) $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning..."
	@$(RM) $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
