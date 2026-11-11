CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2 -D_WIN32_WINNT=0x0601
LDFLAGS = -lm

ifeq ($(OS),Windows_NT)
LDFLAGS += -lwinhttp -lws2_32 -lshlwapi -lshell32 -lole32 -static
EXE = .exe
# Use cmd.exe builtins so it works whether make is invoked from cmd or a POSIX shell
RM = if exist "$(subst /,\,$(1))" rmdir /S /Q "$(subst /,\,$(1))"
RMFILE = if exist "$(subst /,\,$(1))" del /F /Q "$(subst /,\,$(1))"
MKDIR = if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))"
SHELL := cmd.exe
.SHELLFLAGS := /C
else
EXE =
RM = rm -rf "$(1)"
RMFILE = rm -f "$(1)"
MKDIR = mkdir -p "$(1)"
endif

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = .

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/state.c \
          $(SRC_DIR)/auth.c \
          $(SRC_DIR)/config.c \
          $(SRC_DIR)/api.c \
          $(SRC_DIR)/http.c \
          $(SRC_DIR)/json_util.c \
          $(SRC_DIR)/tui.c \
          $(SRC_DIR)/album_art.c \
          $(SRC_DIR)/lyrics.c \
          $(SRC_DIR)/modes.c \
          $(SRC_DIR)/queue_widget.c \
          $(SRC_DIR)/search.c \
          $(SRC_DIR)/library.c \
          $(SRC_DIR)/playlists_ui.c \
          $(SRC_DIR)/queue_screen.c \
          $(SRC_DIR)/modal.c \
          $(SRC_DIR)/settings.c \
          $(SRC_DIR)/input.c \
          $(SRC_DIR)/player.c

HEADERS = $(wildcard $(SRC_DIR)/*.h)

OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

TARGET = $(BIN_DIR)/spoticli$(EXE)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo Linking $@...
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo Build complete: $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	@echo Compiling $<...
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	@$(call MKDIR,$(BUILD_DIR))

clean:
	@echo Cleaning...
	@$(call RM,$(BUILD_DIR))
	@$(call RMFILE,$(TARGET))

run: $(TARGET)
ifeq ($(OS),Windows_NT)
	$(subst /,\,$(TARGET))
else
	./$(TARGET)
endif

# Install so `spoticli` works from any terminal.
#   Windows: copies to %LOCALAPPDATA%\Programs\spoticli and adds it to your user PATH.
#   Other:   copies to ~/.local/bin (override with PREFIX=...).
install: $(TARGET)
ifeq ($(OS),Windows_NT)
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install.ps1 -Exe "$(subst /,\,$(TARGET))"
else
	mkdir -p "$(or $(PREFIX),$(HOME)/.local)/bin"
	cp "$(TARGET)" "$(or $(PREFIX),$(HOME)/.local)/bin/spoticli"
	@echo Installed to $(or $(PREFIX),$(HOME)/.local)/bin/spoticli
endif

uninstall:
ifeq ($(OS),Windows_NT)
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install.ps1 -Uninstall
else
	rm -f "$(or $(PREFIX),$(HOME)/.local)/bin/spoticli"
endif

.PHONY: all clean run install uninstall
