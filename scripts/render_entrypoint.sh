#!/bin/bash
set -e

# Entrypoint script for Docker container
# Renders Lottie animation frames and encodes to video using ffmpeg
# Passes through lotio arguments and adds --stream for video encoding
#
# Usage:
#   - Direct command execution: docker run image ffmpeg -version
#   - lotio rendering: docker run image input.json - 30 --output video.mov

# If first argument is a command (exists in PATH), execute it directly
if [ $# -gt 0 ] && command -v "$1" >/dev/null 2>&1; then
    exec "$@"
fi

# Otherwise, treat arguments as lotio commands and wrap with ffmpeg
# Extract output video file from arguments (entrypoint-specific)
OUTPUT_VIDEO="output.mov"
LOTIO_ARGS=()
FPS=""
TEXT_PADDING="0.97"
TEXT_MEASUREMENT_MODE="accurate"

# Parse arguments: extract --output for video file, pass everything else to lotio
while [[ $# -gt 0 ]]; do
    case $1 in
        --output|-o)
            OUTPUT_VIDEO="$2"
            shift 2
            ;;
        --text-padding|-p)
            TEXT_PADDING="$2"
            shift 2
            ;;
        --text-measurement-mode|-m)
            TEXT_MEASUREMENT_MODE="$2"
            shift 2
            ;;
        --version)
            lotio --version
            exit 0
            ;;
        --help|-h)
            echo "Usage: $0 [LOTIO_OPTIONS] [--output OUTPUT.mov]"
            echo ""
            echo "This script wraps lotio and encodes output to video using ffmpeg."
            echo "All lotio arguments are supported and passed through."
            echo ""
            echo "Additional options:"
            echo "  --output, -o FILE              Output video file (default: output.mov)"
            echo "  --text-padding, -p VALUE       Text padding factor (0.0-1.0, default: 0.97)"
            echo "  --text-measurement-mode, -m MODE  Text measurement mode: fast|accurate|pixel-perfect (default: accurate)"
            echo ""
            echo "lotio usage:"
            lotio --help 2>&1 || echo "  lotio [--stream] [--debug] [--layer-overrides <config.json>] [--text-padding <0.0-1.0>] [--text-measurement-mode <fast|accurate|pixel-perfect>] <input.json> <output_dir> [fps]"
            echo ""
            echo "Note: --stream is automatically added if not present (required for video encoding)"
            echo ""
            echo "Example:"
            echo "  $0 --layer-overrides config.json input.json - 30 --output output.mov"
            echo "  $0 --stream --debug input.json - 25 --output output.mov"
            exit 0
            ;;
        *)
            # Pass all other arguments to lotio (including positional args like fps)
            LOTIO_ARGS+=("$1")
            shift
            ;;
    esac
done

# Extract fps from lotio arguments (it's the last positional argument if it's a number)
# lotio format: [options] <input.json> <output_dir> [fps]
# FPS is the last argument if it's a number and not preceded by a flag
FPS=""
for i in "${!LOTIO_ARGS[@]}"; do
    arg="${LOTIO_ARGS[$i]}"
    # Check if it's a number (fps) - typically the last positional argument
    if [[ "$arg" =~ ^[0-9]+\.?[0-9]*$ ]]; then
        # Check if it's not part of a flag value (previous arg shouldn't be a flag that takes a value)
        is_fps=true
        if [ $i -gt 0 ]; then
            prev_arg="${LOTIO_ARGS[$((i - 1))]}"
            # If previous arg is a flag that takes a value, this isn't fps
            if [[ "$prev_arg" == "--layer-overrides" ]] || \
               [[ "$prev_arg" == "--text-padding" ]] || \
               [[ "$prev_arg" == "-p" ]] || \
               [[ "$prev_arg" == "--text-measurement-mode" ]] || \
               [[ "$prev_arg" == "-m" ]]; then
                is_fps=false
            fi
        fi
        if [ "$is_fps" = true ]; then
            FPS="$arg"
        fi
    fi
done

# Default fps if not found
if [ -z "$FPS" ]; then
    FPS=25
fi

# Ensure --stream is present for video encoding (required for ffmpeg pipe)
HAS_STREAM=false

for arg in "${LOTIO_ARGS[@]}"; do
    if [ "$arg" == "--stream" ]; then
        HAS_STREAM=true
        break
    fi
done

# Add --stream if not present (required for piping to ffmpeg)
if [ "$HAS_STREAM" = false ]; then
    LOTIO_ARGS=("--stream" "${LOTIO_ARGS[@]}")
fi

# Add text padding and measurement mode options
LOTIO_ARGS+=("--text-padding" "$TEXT_PADDING")
LOTIO_ARGS+=("--text-measurement-mode" "$TEXT_MEASUREMENT_MODE")

# Build lotio command with all arguments
LOTIO_CMD=("lotio" "${LOTIO_ARGS[@]}")

echo "[RENDER] Starting frame rendering..."
echo "[RENDER] lotio command: ${LOTIO_CMD[*]}"
echo "[RENDER] Output video: $OUTPUT_VIDEO"
echo "[RENDER] FPS: $FPS"
echo "[RENDER] Text padding: $TEXT_PADDING"
echo "[RENDER] Text measurement mode: $TEXT_MEASUREMENT_MODE"

# Render frames and pipe to ffmpeg
# Use ProRes 4444 codec for transparent MOV with alpha channel support
echo "[RENDER] Rendering frames and encoding to transparent MOV (ProRes 4444)..."
"${LOTIO_CMD[@]}" | /opt/ffmpeg/bin/ffmpeg -y \
    -f image2pipe \
    -vcodec png \
    -r $FPS \
    -i pipe:0 \
    -c:v prores_ks \
    -profile:v 4444 \
    -pix_fmt yuva444p10le \
    "$OUTPUT_VIDEO"

# Check if video was created
if [ -f "$OUTPUT_VIDEO" ]; then
    VIDEO_SIZE=$(du -h "$OUTPUT_VIDEO" | cut -f1)
    echo "[RENDER] Video created successfully: $OUTPUT_VIDEO"
    echo "[RENDER] Video size: $VIDEO_SIZE"
    exit 0
else
    echo "[RENDER] Error: Video file was not created"
    exit 1
fi

