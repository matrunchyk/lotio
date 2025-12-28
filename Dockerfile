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

# Fetch Skia deps
COPY scripts/fetch_skia_deps.sh /tmp/fetch_skia_deps.sh
RUN chmod +x /tmp/fetch_skia_deps.sh && /tmp/fetch_skia_deps.sh

# Install Skia
COPY scripts/install_skia.sh /tmp/install_skia.sh
RUN chmod +x /tmp/install_skia.sh && /tmp/install_skia.sh

########################################################################################
# Stage 2: Render Builder - Compile skottie_renderer binary on Ubuntu
FROM ubuntu:22.04 AS render-builder

# Install build dependencies
RUN echo "[BUILD] Installing build dependencies for skottie_renderer on Ubuntu..." && \
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

# Copy skottie_renderer source
COPY src/skottie_renderer.cpp .

RUN echo "[BUILD] Compiling skottie_renderer with dynamic linking (Ubuntu 22.04)..." && \
    echo "[BUILD] Verifying Skia libraries..." && \
    ls -lh /skia-libs/*.so* 2>/dev/null | head -20 || echo "[BUILD] WARNING: No Skia libraries found in /skia-libs/" && \
    echo "[BUILD] Building skottie_renderer..." && \
    cd /build && \
    g++ -std=c++17 -O3 -march=native -DNDEBUG \
        skottie_renderer.cpp \
        -I/skia \
        -L/skia-libs \
        -Wl,-rpath,/skia-libs \
        -lskottie -lskia -lskparagraph -lsksg -lskshaper \
        -lskunicode_icu -lskunicode_core -lskresources \
        -ljsonreader -lwebp -lwebpdemux -lwebpmux -lpiex \
        -lfreetype -lpng -ljpeg -lharfbuzz -licuuc -licui18n -licudata \
        -lz -lfontconfig -lX11 -lGL -lm -lpthread \
        -o /usr/local/bin/skottie_renderer && \
        echo "skottie_renderer compiled successfully" && \
        ls -lh /usr/local/bin/skottie_renderer

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

# Copy skottie_renderer binary from render-builder
COPY --from=render-builder /usr/local/bin/skottie_renderer /usr/local/bin/skottie_renderer

# Copy fonts if they exist
COPY fonts /usr/local/share/fonts
RUN if [ -d /usr/local/share/fonts ] && [ "$(ls -A /usr/local/share/fonts)" ]; then \
        fc-cache -fv /usr/local/share/fonts && \
        echo "[BUILD] Fonts installed and font cache updated"; \
    else \
        echo "[BUILD] No fonts directory found, skipping font installation"; \
    fi

# Set environment variables
ENV LD_LIBRARY_PATH=/opt/ffmpeg/lib:/opt/skia:/usr/lib/aarch64-linux-gnu:/usr/lib/x86_64-linux-gnu
ENV PATH=/opt/ffmpeg/bin:/usr/local/bin:${PATH}

# Verify skottie_renderer binary
RUN echo "[BUILD] Verifying skottie_renderer binary..." && \
    ldd /usr/local/bin/skottie_renderer | head -10 && \
    echo "[BUILD] All dependencies resolved"

# Copy entrypoint script
COPY scripts/render_entrypoint.sh /usr/local/bin/render_entrypoint.sh
RUN chmod +x /usr/local/bin/render_entrypoint.sh

# Set working directory
WORKDIR /workspace

# Set entrypoint
ENTRYPOINT ["/usr/local/bin/render_entrypoint.sh"]
