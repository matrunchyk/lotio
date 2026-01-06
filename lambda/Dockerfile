# Multi-stage Dockerfile for Lambda function using lotio (Lottie renderer)
# Target: Linux ARM64

# Allow overriding the base lotio-ffmpeg image for local testing
ARG LOTIO_FFMPEG_IMAGE=matrunchyk/lotio-ffmpeg:latest

########################################################################################
# Stage 0: Lotio-FFmpeg Image - Source for lotio binary and ffmpeg
# This stage allows ARG substitution (FROM supports ARG, COPY --from does not)
FROM ${LOTIO_FFMPEG_IMAGE} AS lotio-ffmpeg-source

########################################################################################
# Stage 1: FFmpeg Builder - Build ffmpeg on AL2023 for GLIBC compatibility
FROM public.ecr.aws/lambda/nodejs:22-arm64 AS ffmpeg-builder

# Install build dependencies
RUN dnf install -y \
    gcc \
    gcc-c++ \
    make \
    nasm \
    tar \
    pkgconfig \
    libpng-devel \
    && dnf clean all

# Build ffmpeg with ProRes support (minimal build for lotio's use case)
WORKDIR /ffmpeg-build
RUN curl -L -f https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n8.0.tar.gz -o /tmp/ffmpeg.tar.gz && \
    tar -xz -C /tmp -f /tmp/ffmpeg.tar.gz && \
    cd /tmp/FFmpeg-* && \
    ./configure \
    --prefix=/opt/ffmpeg \
    --enable-decoder=png \
    --enable-demuxer=image2 \
    --enable-muxer=mov \
    --enable-encoder=prores_ks \
    --enable-filter=format \
    --enable-protocol=pipe \
    --disable-network \
    --disable-doc \
    --disable-avdevice \
    --disable-ffplay \
    --disable-ffprobe \
    --extra-cflags="-O3" \
    --extra-ldflags="-Wl,-rpath,/opt/ffmpeg/lib" \
    --enable-shared \
    --disable-static \
    --disable-debug \
    --enable-optimizations \
    && make -j$(nproc) && \
    make install

########################################################################################
# Stage 2: TypeScript Builder - Build Lambda handler
FROM node:22-alpine AS ts-builder

WORKDIR /build

COPY lambda/package.json lambda/package-lock.json* ./
RUN if [ -f package-lock.json ]; then npm ci; else npm install; fi

COPY lambda/tsconfig.json lambda/index.ts ./
RUN npm run build

########################################################################################
# Stage 3: Lambda Runtime - Final image
FROM public.ecr.aws/lambda/nodejs:22-arm64

# Install runtime libraries
RUN dnf install -y \
    fontconfig \
    freetype \
    libX11 \
    libXext \
    libXrender \
    mesa-libGL \
    mesa-libGLU \
    libpng \
    harfbuzz \
    icu \
    && dnf clean all

# Copy lotio binary from lotio-ffmpeg image
# Note: lotio is statically linked against Skia, so Skia libraries are not needed
COPY --from=lotio-ffmpeg-source /usr/local/bin/lotio /opt/bin/lotio

# Copy ffmpeg from AL2023 builder (built with correct GLIBC version)
COPY --from=ffmpeg-builder /opt/ffmpeg /opt/ffmpeg

# Copy ICU libraries from lotio-ffmpeg image (Ubuntu 22.04 has ICU 70, AL2023 has different version)
# lotio binary was built against ICU 70, so we need to use the same version
# BuildKit automatically provides TARGETARCH for multi-arch builds
ARG TARGETARCH
RUN mkdir -p /opt/icu/lib

# Copy ICU libraries using BuildKit mount feature for architecture-specific paths
# This allows us to conditionally copy based on TARGETARCH
RUN --mount=type=bind,from=lotio-ffmpeg-source,source=/usr/lib,target=/mnt/source-lib \
    if [ "$TARGETARCH" = "arm64" ]; then \
        cp -f /mnt/source-lib/aarch64-linux-gnu/libicu*.so* /opt/icu/lib/ 2>/dev/null || true; \
    elif [ "$TARGETARCH" = "amd64" ]; then \
        cp -f /mnt/source-lib/x86_64-linux-gnu/libicu*.so* /opt/icu/lib/ 2>/dev/null || true; \
    fi

# Set environment variables
ENV PATH="/opt/ffmpeg/bin:/opt/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/ffmpeg/lib:/opt/icu/lib:/usr/lib64:${LD_LIBRARY_PATH}"

# Copy fonts if they exist (from repo root)
COPY fonts /usr/local/share/fonts
RUN if [ -d /usr/local/share/fonts ] && [ "$(ls -A /usr/local/share/fonts)" ]; then \
        fc-cache -fv /usr/local/share/fonts; \
    fi

# Copy compiled Lambda code from ts-builder
COPY --from=ts-builder /build/node_modules ${LAMBDA_TASK_ROOT}/node_modules
COPY --from=ts-builder /build/dist/index.js ${LAMBDA_TASK_ROOT}/

# Set handler
CMD [ "index.handler" ]
