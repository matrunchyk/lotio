# syntax=docker/dockerfile:1.7

ARG LOTIO_SUPPLIER_IMAGE=matrunchyk/lotio-al2023:latest
ARG FORCE_REBUILD=0

########################################################################################
# Stage 0: Supplier (lotio + ICU70)
########################################################################################
FROM ${LOTIO_SUPPLIER_IMAGE} AS lotio-supplier
ARG FORCE_REBUILD
RUN echo "FORCE_REBUILD=${FORCE_REBUILD}" >/dev/null

########################################################################################
# Stage 1: FFmpeg builder (build against Lambda runtime GLIBC)
########################################################################################
FROM public.ecr.aws/lambda/nodejs:22 AS ffmpeg-builder
ARG FORCE_REBUILD
RUN echo "FORCE_REBUILD=${FORCE_REBUILD}" >/dev/null

RUN dnf install -y \
    gcc gcc-c++ make nasm tar gzip curl \
    pkgconfig \
    libpng-devel \
    && dnf clean all

WORKDIR /ffmpeg-build

RUN curl -L -f https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n8.0.tar.gz -o /tmp/ffmpeg.tar.gz && \
    tar -xz -C /tmp -f /tmp/ffmpeg.tar.gz && \
    cd /tmp/FFmpeg-* && \
    ./configure \
      --prefix=/opt/ffmpeg \
      --disable-network \
      --disable-doc \
      --disable-avdevice \
      --disable-ffplay \
      --disable-ffprobe \
      --enable-shared \
      --disable-static \
      --disable-debug \
      --enable-optimizations \
      --extra-cflags="-O3" \
      --extra-ldflags="-Wl,-rpath,/opt/ffmpeg/lib" \
      --enable-decoder=png \
      --enable-demuxer=image2 \
      --enable-demuxer=image2pipe \
      --enable-muxer=mov \
      --enable-encoder=prores_ks \
      --enable-filter=format \
      --enable-filter=scale \
      --enable-protocol=pipe \
      --enable-protocol=file \
    && make -j"$(nproc)" \
    && make install

RUN /opt/ffmpeg/bin/ffmpeg -version

########################################################################################
# Stage 2: Bundle builder (lambda/handler.ts -> dist/handler.js)
########################################################################################
FROM node:22-alpine AS bundle-builder
WORKDIR /build

ARG FORCE_REBUILD
RUN echo "FORCE_REBUILD=${FORCE_REBUILD}" >/dev/null

# Copy only lambda package files for stable caching
COPY lambda/package.json lambda/package-lock.json lambda/tsconfig.json lambda/build-bundle.mjs ./
RUN npm ci

# Copy source
COPY lambda/handler.ts ./

# Bundle to dist/handler.js (your script controls format=esm)
RUN node build-bundle.mjs

RUN test -f dist/handler.js || (echo "ERROR: dist/handler.js not found" && ls -la && exit 1)

RUN npm prune --production || true

########################################################################################
# Stage 3: Final runtime (Lambda base)
########################################################################################
FROM public.ecr.aws/lambda/nodejs:22

RUN dnf install -y \
    fontconfig \
    freetype \
    libpng \
    harfbuzz \
    && dnf clean all

# lotio + ICU70 from supplier (this is the contract you already validated)
COPY --from=lotio-supplier /usr/local/bin/lotio /opt/bin/lotio
COPY --from=lotio-supplier /opt/icu /opt/icu

# ffmpeg built for Lambda base glibc
COPY --from=ffmpeg-builder /opt/ffmpeg /opt/ffmpeg

ENV PATH="/opt/ffmpeg/bin:/opt/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/ffmpeg/lib:/opt/icu/lib:/usr/lib64:${LD_LIBRARY_PATH}"

RUN test -f /opt/icu/lib/libicuuc.so.70 || (echo "ERROR: ICU70 missing in /opt/icu/lib" && ls -la /opt/icu/lib && exit 1)

# fonts
COPY lambda/fonts /usr/local/share/fonts
RUN if [ -d /usr/local/share/fonts ] && [ "$(ls -A /usr/local/share/fonts)" ]; then \
      fc-cache -fv /usr/local/share/fonts; \
    fi

# handler + deps
COPY --from=bundle-builder /build/dist/handler.js ${LAMBDA_TASK_ROOT}/handler.js
COPY --from=bundle-builder /build/node_modules ${LAMBDA_TASK_ROOT}/node_modules

WORKDIR ${LAMBDA_TASK_ROOT}
RUN echo '{"type":"module"}' > package.json

CMD [ "handler.handler" ]
