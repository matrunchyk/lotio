# Makefile for Lottie Frame Renderer
# Local build system matching Docker build process

CXX = clang++
CXXFLAGS = -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra
PROJECT_ROOT = $(shell pwd)
SKIA_ROOT = $(PROJECT_ROOT)/third_party/skia/skia
SKIA_LIB_DIR = $(SKIA_ROOT)/out/Release
SRC_DIR = $(PROJECT_ROOT)/src

# Detect OS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS
    FRAMEWORKS = -framework CoreFoundation -framework CoreGraphics -framework CoreText -framework CoreServices -framework AppKit
    RPATH_FLAG = -Wl,-rpath,$(SKIA_LIB_DIR)
else
    # Linux
    FRAMEWORKS =
    RPATH_FLAG = -Wl,-rpath,$(SKIA_LIB_DIR)
    LIBS_X11 = -lX11 -lGL -lGLU
endif

INCLUDES = -I$(SKIA_ROOT) -I$(SRC_DIR)
LDFLAGS = -L$(SKIA_LIB_DIR) $(RPATH_FLAG)

# Skia libraries (matching Docker build)
SKIA_LIBS = -lskottie -lskia -lskparagraph -lsksg -lskshaper \
            -lskunicode_icu -lskunicode_core -lskresources \
            -ljsonreader -lwebp -lwebpdemux -lwebpmux -lpiex \
            -lfreetype -lpng -ljpeg -lharfbuzz -licuuc -licui18n -licudata \
            -lz -lfontconfig -lm -lpthread $(FRAMEWORKS) $(LIBS_X11)

# Source files
SOURCES = $(SRC_DIR)/main.cpp \
          $(SRC_DIR)/core/argument_parser.cpp \
          $(SRC_DIR)/core/animation_setup.cpp \
          $(SRC_DIR)/core/frame_encoder.cpp \
          $(SRC_DIR)/core/renderer.cpp \
          $(SRC_DIR)/utils/crash_handler.cpp \
          $(SRC_DIR)/utils/logging.cpp \
          $(SRC_DIR)/utils/string_utils.cpp \
          $(SRC_DIR)/text/text_config.cpp \
          $(SRC_DIR)/text/text_processor.cpp \
          $(SRC_DIR)/text/font_utils.cpp \
          $(SRC_DIR)/text/text_sizing.cpp \
          $(SRC_DIR)/text/json_manipulation.cpp

OBJECTS = $(SOURCES:.cpp=.o)
TARGET = lotio

.PHONY: all clean check-skia build help

all: check-skia $(TARGET)

help:
	@echo "Makefile for Lottie Frame Renderer"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build the project (checks Skia first)"
	@echo "  make build    - Same as 'make'"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make check-skia - Check if Skia is built"

check-skia:
	@if [ ! -f "$(SKIA_LIB_DIR)/libskia.a" ] && [ ! -f "$(SKIA_LIB_DIR)/libskia.dylib" ] && [ ! -f "$(SKIA_LIB_DIR)/libskia.so" ]; then \
		echo "❌ Skia library not found at $(SKIA_LIB_DIR)"; \
		echo "📦 Building Skia first..."; \
		./install_skia.sh; \
	fi

build: all

$(TARGET): $(OBJECTS)
	@echo "🔗 Linking..."
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS) $(SKIA_LIBS)
	@echo "✅ Build complete: $(TARGET)"

%.o: %.cpp
	@echo "   Compiling: $(notdir $<)"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@echo "🧹 Cleaning..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "✅ Clean complete"
