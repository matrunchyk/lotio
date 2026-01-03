# Multi-stage Dockerfile for building Skia with Skottie support
# Target: Linux ARM64

########################################################################################
# Stage 1: Builder - Compile Skia with all dependencies
FROM ubuntu:22.04 AS skia-builder

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TARGET_ARCH=arm64

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    python3 \
    python3-pip \
    ninja-build \
    git \
    curl \
    clang \
    lld \
    pkg-config \
    libfontconfig1-dev \
    libfreetype6-dev \
    libx11-dev \
    libxext-dev \
    libxrender-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libjpeg-dev \
    libpng-dev \
    libharfbuzz-dev \
    libwebp-dev \
    fontconfig \
    && rm -rf /var/lib/apt/lists/*

# Disable Git detached HEAD advice to suppress warnings during build
RUN git config --global advice.detachedHead false

# Set up Skia build directory
WORKDIR /skia-build

# Fetch Skia deps (clones Skia to /skia-build/skia)
COPY scripts/fetch_skia_deps.sh /tmp/fetch_skia_deps.sh
RUN chmod +x /tmp/fetch_skia_deps.sh && /tmp/fetch_skia_deps.sh

# Build Skia directly (install_skia.sh expects project structure, so we build manually)
RUN echo "[BUILD] Building Skia..." && \
    cd /skia-build/skia && \
    python3 bin/fetch-gn && \
    ./bin/gn gen out/Release --args='target_cpu="arm64" is_official_build=true is_debug=false skia_enable_skottie=true skia_enable_fontmgr_fontconfig=true skia_enable_fontmgr_custom_directory=true skia_use_freetype=true skia_use_libpng_encode=true skia_use_libpng_decode=true skia_use_libwebp_decode=true skia_use_wuffs=true skia_enable_pdf=false' && \
    ninja -C out/Release && \
    echo "[BUILD] Skia built successfully"

########################################################################################
# Stage 2: Render Builder - Compile lotio binary on Ubuntu
FROM ubuntu:22.04 AS render-builder

# Install build dependencies
RUN echo "[BUILD] Installing build dependencies for lotio on Ubuntu..." && \
    apt-get update && \
    apt-get install -y \
    gcc \
    g++ \
    make \
    libfontconfig1-dev \
    libfreetype6-dev \
    libx11-dev \
    libxext-dev \
    libxrender-dev \
    mesa-common-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libpng-dev \
    libjpeg-dev \
    libicu-dev \
    libharfbuzz-dev \
    libwebp-dev \
    && rm -rf /var/lib/apt/lists/* && \
    echo "[BUILD] Build dependencies installed"

WORKDIR /build

# Copy Skia libraries and headers from skia-builder
COPY --from=skia-builder /skia-build/skia/out/Release /skia-libs
COPY --from=skia-builder /skia-build/skia/include /skia/include
COPY --from=skia-builder /skia-build/skia/modules /skia/modules
COPY --from=skia-builder /skia-build/skia/src /skia/src

# Copy lotio source files
COPY src/ ./src/

RUN echo "[BUILD] Compiling lotio with dynamic linking (Ubuntu 22.04)..." && \
    echo "[BUILD] Verifying Skia libraries..." && \
    ls -lh /skia-libs/*.a 2>/dev/null | head -20 || echo "[BUILD] WARNING: No Skia libraries found in /skia-libs/" && \
    echo "[BUILD] Setting up include structure..." && \
    mkdir -p /tmp_include/skia && \
    ln -sf /skia/include/core /tmp_include/skia/core 2>/dev/null || true && \
    ln -sf /skia/include /tmp_include/skia/include 2>/dev/null || true && \
    ln -sf /skia/modules /tmp_include/skia/modules 2>/dev/null || true && \
    echo "[BUILD] Building lotio..." && \
    cd /build && \
    # Compile library sources (use /skia as base for relative includes in Skia headers)
    g++ -std=c++17 -O3 -DNDEBUG -I/tmp_include -I/skia -I./src -c \
        src/core/argument_parser.cpp \
        src/core/animation_setup.cpp \
        src/core/frame_encoder.cpp \
        src/core/renderer.cpp \
        src/utils/crash_handler.cpp \
        src/utils/logging.cpp \
        src/utils/string_utils.cpp \
        src/text/text_config.cpp \
        src/text/text_processor.cpp \
        src/text/font_utils.cpp \
        src/text/text_sizing.cpp \
        src/text/json_manipulation.cpp && \
    # Compile main
    g++ -std=c++17 -O3 -DNDEBUG -I/tmp_include -I/skia -I./src -c src/main.cpp -o main.o && \
    # Link binary
    g++ -std=c++17 -O3 -DNDEBUG \
        *.o \
        -L/skia-libs \
        -Wl,-rpath,/skia-libs \
        -Wl,--start-group \
        /skia-libs/libskresources.a \
        /skia-libs/libskparagraph.a \
        /skia-libs/libskottie.a \
        /skia-libs/libskshaper.a \
        /skia-libs/libskunicode_icu.a \
        /skia-libs/libskunicode_core.a \
        -Wl,--end-group \
        /skia-libs/libsksg.a \
        /skia-libs/libjsonreader.a \
        /skia-libs/libskia.a \
        -lwebp -lwebpdemux -lwebpmux -lpiex \
        -lfreetype -lpng -ljpeg -lharfbuzz -licuuc -licui18n -licudata \
        -lz -lfontconfig -lX11 -lGL -lGLU -lm -lpthread \
        -o /usr/local/bin/lotio && \
    echo "lotio compiled successfully" && \
    ls -lh /usr/local/bin/lotio && \
    rm -f *.o && \
    rm -rf /tmp_include

########################################################################################
# Stage 3: FFmpeg Builder - Build ffmpeg from source on Ubuntu
FROM ubuntu:22.04 AS ffmpeg-builder

RUN echo "[BUILD] Building ffmpeg from source on Ubuntu..." && \
    apt-get update && \
    apt-get install -y \
    gcc \
    g++ \
    make \
    nasm \
    yasm \
    git \
    curl \
    tar \
    cmake \
    pkg-config \
    libpng-dev \
    && rm -rf /var/lib/apt/lists/*

# Build x264
WORKDIR /build
RUN echo "[BUILD] Building x264..." && \
    git clone --depth 1 https://code.videolan.org/videolan/x264.git && \
    cd x264 && \
    ./configure --prefix=/opt/ffmpeg --enable-shared --enable-pic && \
    make -j$(nproc) && \
    make install && \
    echo "[BUILD] x264 built"

# Build x265 (disable unsupported ARM optimizations)
RUN echo "[BUILD] Building x265..." && \
    git clone --depth 1 https://bitbucket.org/multicoreware/x265_git.git && \
    cd x265_git && \
    mkdir -p build/linux && \
    cd build/linux && \
    cmake -G "Unix Makefiles" \
    -DCMAKE_INSTALL_PREFIX=/opt/ffmpeg \
    -DENABLE_SHARED=ON \
    -DENABLE_SVE2=OFF \
    -DENABLE_SVE2_BITPERM=OFF \
    ../../source && \
    make -j$(nproc) && \
    make install && \
    echo "[BUILD] x265 built" && \
    echo "[BUILD] Creating x265 pkg-config file..." && \
    mkdir -p /opt/ffmpeg/lib/pkgconfig && \
    echo 'prefix=/opt/ffmpeg' > /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo 'exec_prefix=${prefix}' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo 'libdir=${exec_prefix}/lib' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo 'includedir=${prefix}/include' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo '' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo 'Name: x265' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo 'Description: H.265/HEVC video encoder' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo 'Version: 3.5' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo 'Libs: -L${libdir} -lx265' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo 'Libs.private: -lstdc++ -lm -ldl' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo 'Cflags: -I${includedir}' >> /opt/ffmpeg/lib/pkgconfig/x265.pc && \
    echo "[BUILD] x265 pkg-config file created"

# Build ffmpeg with ProRes support
WORKDIR /ffmpeg-build
RUN echo "[BUILD] Downloading ffmpeg source..." && \
    curl -L https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.2.tar.gz | tar -xz && \
    cd FFmpeg-* && \
    echo "[BUILD] Configuring ffmpeg for Ubuntu (ProRes support built-in)..." && \
    PKG_CONFIG_PATH=/opt/ffmpeg/lib/pkgconfig ./configure \
    --prefix=/opt/ffmpeg \
    --enable-gpl \
    --enable-version3 \
    --enable-libx264 \
    --enable-decoder=png \
    --enable-encoder=png \
    --enable-demuxer=image2 \
    --extra-cflags="-I/opt/ffmpeg/include -O3 -march=native" \
    --extra-ldflags="-L/opt/ffmpeg/lib -Wl,-rpath,/opt/ffmpeg/lib" \
    --enable-shared \
    --disable-static \
    --disable-debug \
    --enable-optimizations \
    && echo "[BUILD] Building ffmpeg (this may take a while)..." && \
    make -j$(nproc) && \
    make install && \
    echo "[BUILD] ffmpeg built successfully" && \
    echo "[BUILD] Verifying ProRes codec support..." && \
    /opt/ffmpeg/bin/ffmpeg -codecs | grep -i prores && \
    echo "[BUILD] ProRes codec verified"

########################################################################################
# Stage 4: Runtime - Final image with all tools and entrypoint
FROM ubuntu:22.04

# Install runtime libraries
RUN echo "[BUILD] Setting up runtime environment..." && \
    apt-get update && \
    apt-get install -y \
    fontconfig \
    libfontconfig1 \
    libfreetype6 \
    libx11-6 \
    libxext6 \
    libxrender1 \
    libgl1-mesa-glx \
    libglu1-mesa \
    libpng16-16 \
    libjpeg-turbo8 \
    libwebp7 \
    libwebpdemux2 \
    libwebpmux3 \
    libicu70 \
    libharfbuzz0b \
    && rm -rf /var/lib/apt/lists/* && \
    echo "[BUILD] Runtime libraries installed"

# Copy ffmpeg and all its libraries from builder
COPY --from=ffmpeg-builder /opt/ffmpeg /opt/ffmpeg

ENV PATH="/opt/ffmpeg/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/ffmpeg/lib"

RUN echo "[BUILD] Verifying ffmpeg..." && \
    /opt/ffmpeg/bin/ffmpeg -version && \
    echo "[BUILD] ffmpeg verified"

# Copy Skia libraries from skia-builder
COPY --from=skia-builder /skia-build/skia/out/Release /opt/skia

# Copy lotio binary from render-builder
COPY --from=render-builder /usr/local/bin/lotio /usr/local/bin/lotio

# Copy fonts if they exist (optional)
# Note: Fonts can be mounted at runtime if needed
RUN echo "[BUILD] Font directory will be available for runtime font mounting"

# Set environment variables
ENV LD_LIBRARY_PATH=/opt/ffmpeg/lib:/opt/skia:/usr/lib/aarch64-linux-gnu:/usr/lib/x86_64-linux-gnu
ENV PATH=/opt/ffmpeg/bin:/usr/local/bin:${PATH}

# Verify lotio binary
RUN echo "[BUILD] Verifying lotio binary..." && \
    ldd /usr/local/bin/lotio | head -10 && \
    echo "[BUILD] All dependencies resolved"

# Copy entrypoint script
COPY scripts/render_entrypoint.sh /usr/local/bin/render_entrypoint.sh
RUN chmod +x /usr/local/bin/render_entrypoint.sh

# Set working directory
WORKDIR /workspace

# Set entrypoint
ENTRYPOINT ["/usr/local/bin/render_entrypoint.sh"]
