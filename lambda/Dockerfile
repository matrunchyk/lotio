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
# Stage 2: Development - Image with build tools for compiling user programs
# Note: We use a separate image for development to avoid bloating the skia-builder stage
#   image with development dependencies.
FROM ubuntu:22.04 AS development

# Install build tools and runtime dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    libfontconfig1 \
    libfontconfig1-dev \
    libfreetype6 \
    libfreetype6-dev \
    libx11-6 \
    libx11-dev \
    libxext6 \
    libxext-dev \
    libxrender1 \
    libharfbuzz-dev \
    libxrender-dev \
    libgl1-mesa-glx \
    libgl1-mesa-dev \
    libglu1-mesa \
    libwebp-dev \
    libpng-dev \
    libjpeg-dev \
    libicu-dev \
    fontconfig \
    && rm -rf /var/lib/apt/lists/*

# Copy built binaries and libraries from skia-builder stage
COPY --from=skia-builder /skia-build/skia/out/Release /skia/out/Release

# Copy necessary header files, modules, and source files
COPY --from=skia-builder /skia-build/skia/include /skia/include
COPY --from=skia-builder /skia-build/skia/modules /skia/modules
COPY --from=skia-builder /skia-build/skia/src /skia/src


# Set working directory
WORKDIR /workspace

# Copy lotio_renderer.cpp source file (for reference, will be copied again in render-builder stage)
COPY src/lotio_renderer.cpp /workspace/

# Set library path for runtime
ENV LD_LIBRARY_PATH="/skia/out/Release"

########################################################################################
# Stage 3: ICU Library Extractor - Extract ICU libraries from Ubuntu builder
FROM ubuntu:22.04 AS icu-extractor

RUN echo "[BUILD] Extracting ICU libraries from Ubuntu 22.04..." && \
    apt-get update && \
    apt-get install -y libicu70 libicu-dev && \
    mkdir -p /opt/icu/lib && \
    echo "[BUILD] Finding ICU libraries..." && \
    find /usr/lib -name "libicu*.so*" -type f -exec cp -v {} /opt/icu/lib/ \; && \
    find /usr/lib/aarch64-linux-gnu -name "libicu*.so*" -type f -exec cp -v {} /opt/icu/lib/ \; 2>/dev/null || true && \
    find /usr/lib/x86_64-linux-gnu -name "libicu*.so*" -type f -exec cp -v {} /opt/icu/lib/ \; 2>/dev/null || true && \
    echo "[BUILD] ICU libraries found:" && \
    ls -lh /opt/icu/lib/ && \
    echo "[BUILD] Verifying ICU 70 libraries exist..." && \
    (test -f /opt/icu/lib/libicuuc.so.70 && echo "[BUILD] ✓ libicuuc.so.70 found" || echo "[BUILD] ✗ libicuuc.so.70 NOT found") && \
    (test -f /opt/icu/lib/libicui18n.so.70 && echo "[BUILD] ✓ libicui18n.so.70 found" || echo "[BUILD] ✗ libicui18n.so.70 NOT found") && \
    (test -f /opt/icu/lib/libicudata.so.70 && echo "[BUILD] ✓ libicudata.so.70 found" || echo "[BUILD] ✗ libicudata.so.70 NOT found") && \
    rm -rf /var/lib/apt/lists/* && \
    echo "[BUILD] ICU libraries extracted"

########################################################################################
# Stage 4: Render Builder - Compile lotio binary on AL2023 (GLIBC compatible)
# We need to build on AL2023 to match Lambda runtime's GLIBC version
FROM public.ecr.aws/amazonlinux/amazonlinux:2023 AS render-builder

# Install build dependencies
RUN echo "[BUILD] Installing build dependencies for lotio on AL2023..." && \
    dnf install -y \
    gcc \
    gcc-c++ \
    make \
    fontconfig-devel \
    freetype-devel \
    libX11-devel \
    libXext-devel \
    libXrender-devel \
    mesa-libGL-devel \
    mesa-libGLU-devel \
    libpng-devel \
    libjpeg-turbo-devel \
    libicu-devel \
    && dnf clean all && \
    echo "[BUILD] Build dependencies installed"

WORKDIR /build

# Copy Skia libraries and headers from skia-builder
COPY --from=skia-builder /skia-build/skia/out/Release /skia-libs
COPY --from=skia-builder /skia-build/skia/include /skia/include
COPY --from=skia-builder /skia-build/skia/modules /skia/modules
COPY --from=skia-builder /skia-build/skia/src /skia/src

# Copy ICU libraries from icu-extractor (Ubuntu 22.04, matching Skia's ICU version)
COPY --from=icu-extractor /opt/icu/lib /opt/icu/lib

RUN echo "[BUILD] Verifying ICU libraries..." && \
    ls -la /opt/icu/lib/ 2>/dev/null | head -10 || echo "[BUILD] No ICU libraries found, will use system ICU"

# Copy lotio_renderer source
COPY src/lotio_renderer.cpp .

RUN echo "[BUILD] Compiling lotio with dynamic linking (AL2023, GLIBC 2.34)..." && \
    echo "[BUILD] Verifying Skia headers..." && \
    ls -la /skia/include/ | head -5 && \
    ls -la /skia/modules/ | head -5 && \
    echo "[BUILD] Checking Skia libraries in /skia-libs..." && \
    ls -lh /skia-libs/*.so* 2>/dev/null | head -20 || echo "[BUILD] WARNING: No Skia libraries found in /skia-libs/" && \
    echo "[BUILD] Checking ICU libraries..." && \
    ls -lh /opt/icu/lib/*.so* 2>/dev/null | head -10 || echo "[BUILD] WARNING: No ICU libraries found in /opt/icu/lib/" && \
    echo "[BUILD] Creating ICU symlinks if needed..." && \
    (cd /opt/icu/lib && \
     (test -f libicuuc.so.70.1 && ! test -f libicuuc.so.70 && ln -s libicuuc.so.70.1 libicuuc.so.70 && echo "[BUILD] Created libicuuc.so.70 symlink") || true && \
     (test -f libicui18n.so.70.1 && ! test -f libicui18n.so.70 && ln -s libicui18n.so.70.1 libicui18n.so.70 && echo "[BUILD] Created libicui18n.so.70 symlink") || true && \
     (test -f libicudata.so.70.1 && ! test -f libicudata.so.70 && ln -s libicudata.so.70.1 libicudata.so.70 && echo "[BUILD] Created libicudata.so.70 symlink") || true) && \
    echo "[BUILD] Verifying required ICU 70 libraries..." && \
    ICU_UC="/opt/icu/lib/libicuuc.so.70" && \
    ICU_I18N="/opt/icu/lib/libicui18n.so.70" && \
    ICU_DATA="/opt/icu/lib/libicudata.so.70" && \
    if [ ! -f "$ICU_UC" ] && [ ! -f "/opt/icu/lib/libicuuc.so.70.1" ]; then \
        echo "[BUILD] ERROR: libicuuc.so.70 not found!"; \
        exit 1; \
    fi && \
    if [ ! -f "$ICU_I18N" ] && [ ! -f "/opt/icu/lib/libicui18n.so.70.1" ]; then \
        echo "[BUILD] ERROR: libicui18n.so.70 not found!"; \
        exit 1; \
    fi && \
    if [ ! -f "$ICU_DATA" ] && [ ! -f "/opt/icu/lib/libicudata.so.70.1" ]; then \
        echo "[BUILD] ERROR: libicudata.so.70 not found!"; \
        exit 1; \
    fi && \
    echo "[BUILD] ✓ All ICU 70 libraries found" && \
    echo "[BUILD] Checking ICU symbols (verifying _70 suffix)..." && \
    echo "[BUILD] Checking libicuuc.so.70 for required symbols..." && \
    (nm -D "$ICU_UC" 2>/dev/null | grep -E "ubidi_getLength_70|ubrk_open_70|utext_openUTF8_70" | head -5 && echo "[BUILD] ✓ Found ICU 70 symbols in libicuuc") || \
    (echo "[BUILD] ✗ WARNING: ICU 70 symbols not found in libicuuc, checking alternative names..." && \
     nm -D "$ICU_UC" 2>/dev/null | grep -E "ubidi_getLength|ubrk_open|utext_openUTF8" | head -5 || echo "[BUILD] No ICU symbols found") && \
    echo "[BUILD] Checking libicui18n.so.70 for required symbols..." && \
    (nm -D "$ICU_I18N" 2>/dev/null | grep -E "ubidi_|ubrk_" | head -3 || echo "[BUILD] ICU i18n symbol check skipped") && \
    echo "[BUILD] Installing additional dependencies for linking..." && \
    dnf install -y harfbuzz-devel libwebp-devel && \
    dnf clean all && \
    echo "[BUILD] Listing available Skia libraries..." && \
    find /skia-libs -name "*.so*" -o -name "*.a" | sort && \
    echo "[BUILD] Checking for jsonreader and webp libraries..." && \
    (find /skia-libs -name "*json*" -o -name "*webp*" | head -10 || echo "[BUILD] No json/webp libraries found in /skia-libs") && \
    echo "[BUILD] Building lotio..." && \
    cd /build && \
    g++ -std=c++17 -O3 -march=native -DNDEBUG \
        lotio_renderer.cpp \
        -I/skia \
        -L/skia-libs \
        -Wl,-rpath,/skia-libs \
        -L/opt/icu/lib \
        -Wl,-rpath,/opt/icu/lib \
        -lskottie -lskia -lskparagraph -lsksg -lskshaper \
        -lskunicode_icu -lskunicode_core -lskresources \
        -ljsonreader -lwebp -lwebpdemux -lwebpmux -lpiex \
        -lfreetype -lpng -ljpeg -lharfbuzz -lz -lfontconfig -lX11 -lGL -lm -lpthread \
        "$ICU_UC" "$ICU_I18N" "$ICU_DATA" \
        -o /usr/local/bin/lotio

########################################################################################
# Stage 5: TypeScript Builder - Build Lambda handler
FROM node:22-alpine AS ts-builder

WORKDIR /build

COPY lambda/package.json lambda/package-lock.json* ./
RUN echo "[BUILD] Installing Node.js dependencies..." && \
    if [ -f package-lock.json ]; then \
        npm ci; \
    else \
        npm install; \
    fi && \
    echo "[BUILD] Dependencies installed"

COPY lambda/tsconfig.json ./
COPY lambda/index.ts ./

RUN echo "[BUILD] Compiling TypeScript..." && \
    npm run build && \
    echo "[BUILD] TypeScript compilation complete"

########################################################################################
# Stage 6: FFmpeg Builder - Build ffmpeg from source on AL2023 (GLIBC compatible)
# Using AL2023 to match Lambda runtime's GLIBC version (2.34)
FROM public.ecr.aws/amazonlinux/amazonlinux:2023 AS ffmpeg-builder

RUN echo "[BUILD] Building ffmpeg from source on AL2023..." && \
    dnf install -y --allowerasing \
    gcc \
    gcc-c++ \
    make \
    nasm \
    yasm \
    git \
    curl \
    tar \
    cmake \
    pkgconfig \
    libpng-devel \
    && dnf clean all

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
    echo "[BUILD] Configuring ffmpeg for AL2023 (ProRes support built-in, no x265 needed)..." && \
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
# Stage 7: Lambda Runtime - Final image
FROM public.ecr.aws/lambda/nodejs:22-arm64

# Install runtime libraries (ffmpeg will be copied from builder)
# Note: Lambda base image uses dnf (Amazon Linux 2023)
RUN echo "[BUILD] Setting up Lambda runtime environment..." && \
    echo "[BUILD] Installing runtime libraries..." && \
    dnf install -y \
    fontconfig \
    freetype \
    libX11 \
    libXext \
    libXrender \
    mesa-libGL \
    mesa-libGLU \
    libpng \
    libjpeg-turbo \
    libwebp \
    icu \
    && echo "[BUILD] Runtime libraries installed" && \
    dnf clean all

# Copy ffmpeg and all its libraries from builder (built on AL2023, GLIBC compatible)
COPY --from=ffmpeg-builder /opt/ffmpeg /opt/ffmpeg

ENV PATH="/opt/ffmpeg/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/ffmpeg/lib:/usr/lib64:${LD_LIBRARY_PATH}"

RUN echo "[BUILD] Verifying ffmpeg..." && \
    /opt/ffmpeg/bin/ffmpeg -version && \
    echo "[BUILD] ffmpeg verified"

# Copy Skia libraries from skia-builder
COPY --from=skia-builder /skia-build/skia/out/Release /opt/skia

# Copy ICU libraries from render-builder (matching Skia's ICU version from Ubuntu)
COPY --from=render-builder /opt/icu/lib /opt/icu/lib

# Copy lotio binary from render-builder (built on AL2023, GLIBC compatible)
COPY --from=render-builder /usr/local/bin/lotio /opt/bin/lotio

# Copy fonts if they exist (from repo root)
COPY fonts /usr/local/share/fonts
RUN if [ -d /usr/local/share/fonts ] && [ "$(ls -A /usr/local/share/fonts)" ]; then \
        fc-cache -fv /usr/local/share/fonts && \
        echo "[BUILD] Fonts installed and font cache updated"; \
    else \
        echo "[BUILD] No fonts directory found, skipping font installation"; \
    fi

# Set environment variables
ENV LD_LIBRARY_PATH=/opt/ffmpeg/lib:/opt/skia:/opt/icu/lib:/usr/lib64:${LD_LIBRARY_PATH}
ENV PATH=/opt/ffmpeg/bin:/opt/bin:${PATH}

# Verify lotio binary
RUN echo "[BUILD] Verifying lotio binary..." && \
    ldd /opt/bin/lotio && \
    echo "[BUILD] All dependencies resolved"

# Copy compiled Lambda code from ts-builder
COPY --from=ts-builder /build/node_modules ${LAMBDA_TASK_ROOT}/node_modules
COPY --from=ts-builder /build/dist/index.js ${LAMBDA_TASK_ROOT}/

# Verify lotio binary is accessible
RUN echo "[BUILD] Verifying lotio binary..." && \
    ls -lh /opt/bin/lotio && \
    ldd /opt/bin/lotio | head -10 && \
    echo "[BUILD] Binary verified"

# Log final image contents
RUN echo "[BUILD] Final image contents:" && \
    du -sh /opt/skia /opt/bin /opt/ffmpeg && \
    echo "[BUILD] Image setup complete"

# Set handler
CMD [ "index.handler" ]

