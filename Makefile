# Makefile for P2P project with FFmpeg and Windows GDI
# For MSYS64/MinGW environment

# Compiler settings
CC = gcc
CFLAGS = -Wall -O2 -DWIN32DLL -Iffmpeg/include
LDFLAGS = -LLib -Lffmpeg/lib
LIBS = -lPPCS_API -lavcodec -lavutil -lswscale -lws2_32 -lgdi32 -luser32
INCLUDES = -IInclude

# Target executable
TARGET = p2p-client.exe

# Source files
SOURCES = p2p-client.c video_decoder.c video_display_gdi.c control_panel.c cJSON.c

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Default target
all: $(TARGET) copy_dlls

# Link the executable
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS) $(LIBS)
	@echo "Build successful!"

# Compile source files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning..."
	@if exist *.o del /Q *.o 2>nul
	@if exist $(TARGET) del /Q $(TARGET) 2>nul
	@echo "Clean complete!"

# Run the program
run: $(TARGET)
	@echo "Running $(TARGET)..."
	./$(TARGET)

# Help message
help:
	@echo "Available targets:"
	@echo "  make          - Build the project"
	@echo "  make all      - Build the project and copy DLLs"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make run      - Build and run the program"
	@echo "  make help     - Show this help message"

.PHONY: all clean run copy_dlls help
