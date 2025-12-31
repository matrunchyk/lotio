# Makefile for Lottie Frame Renderer
# Delegates to build_local.sh to avoid duplication
# This provides a convenient 'make' interface while using the same build logic as CI/CD

.PHONY: all build clean check-skia help

all: build

build:
	@chmod +x scripts/build_local.sh
	@./scripts/build_local.sh

check-skia:
	@if [ ! -f "third_party/skia/skia/out/Release/libskia.a" ] && [ ! -f "third_party/skia/skia/out/Release/libskia.dylib" ] && [ ! -f "third_party/skia/skia/out/Release/libskia.so" ]; then \
		echo "❌ Skia library not found"; \
		echo "📦 Building Skia first..."; \
		./scripts/install_skia.sh; \
	fi

clean:
	@echo "🧹 Cleaning..."
	@rm -f lotio
	@rm -f liblotio.a
	@rm -f src/**/*.o src/*.o 2>/dev/null || true
	@echo "✅ Clean complete"

help:
	@echo "Makefile for Lottie Frame Renderer"
	@echo ""
	@echo "This Makefile delegates to scripts/build_local.sh to avoid duplication."
	@echo "The same build script is used for local development and CI/CD."
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build the project (uses build_local.sh)"
	@echo "  make build    - Same as 'make'"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make check-skia - Check if Skia is built"
	@echo ""
	@echo "For more control, use directly:"
	@echo "  ./scripts/build_local.sh"
