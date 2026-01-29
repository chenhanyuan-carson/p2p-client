# Updated Makefile for modular structure

# Compiler settings
CC = gcc
CFLAGS = -Wall -O2 -DWIN32DLL -finput-charset=UTF-8 -fexec-charset=GBK -Iffmpeg/include
LDFLAGS = -LLib -Lffmpeg/lib
LIBS = -lPPCS_API -lavcodec -lavutil -lswscale -lws2_32 -lgdi32 -luser32 -lcomctl32
INCLUDES = -I. -IInclude -Isrc/ppcs -Isrc/json -Isrc/image -Isrc/video -Isrc/signaling -Isrc/app -Isrc/control_panel

# Output directory
BIN_DIR = bin

# Target executable
TARGET = $(BIN_DIR)/p2p-client.exe

# Source files
SOURCES = \
	src/main.c \
	src/ppcs/ppcs_core.c \
	src/signaling/command_handler.c \
	src/image/image_handler.c \
	src/video/video_manager.c \
	src/video/video_decoder.c \
	src/video/video_display.c \
	src/control_panel/control_panel.c \
	src/control_panel/control_panel_tab.c \
	src/json/cJSON.c

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Default target
all: $(TARGET)

# Create bin directory
$(BIN_DIR):
	@if not exist $(BIN_DIR) mkdir $(BIN_DIR)

# Link the executable
$(TARGET): $(OBJECTS) | $(BIN_DIR)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS) $(LIBS)
	@echo "Build successful!"

# Compile source files
$(OBJECTS): %.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning..."
	@if exist *.o del /Q *.o 2>nul
	@if exist $(BIN_DIR) rmdir /S /Q $(BIN_DIR) 2>nul
	@echo "Clean complete!"

# Run the program
run: $(TARGET)
	@echo "Running $(TARGET)..."
	@cd $(BIN_DIR) && p2p-client.exe

# Help message
help:
	@echo "Available targets:"
	@echo "  make          - Build the project (output to bin/)"
	@echo "  make all      - Build the project"
	@echo "  make clean    - Remove build artifacts and bin/ directory"
	@echo "  make run      - Build and run the program from bin/"
	@echo "  make help     - Show this help message"

.PHONY: all clean run help
