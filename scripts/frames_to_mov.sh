#!/bin/bash
set -e

# Script to convert a folder of PNG frames to ProRes 4444 transparent MOV
#
# Usage:
#   frames_to_mov.sh <frames_folder> [--fps FPS] [--output OUTPUT.mov] [--pattern PATTERN]
#
# Arguments:
#   frames_folder    Path to folder containing PNG frames
#   --fps, -f        Frame rate (default: 25)
#   --output, -o     Output video file (default: frames_folder.mov)
#   --pattern, -p    Frame filename pattern (default: frame_%04d.png)
#   --help, -h       Show this help message

FRAMES_FOLDER=""
FPS=25
OUTPUT_VIDEO=""
PATTERN="frame_%04d.png"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --fps|-f)
            FPS="$2"
            shift 2
            ;;
        --output|-o)
            OUTPUT_VIDEO="$2"
            shift 2
            ;;
        --pattern|-p)
            PATTERN="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 <frames_folder> [OPTIONS]"
            echo ""
            echo "Convert a folder of PNG frames to ProRes 4444 transparent MOV"
            echo ""
            echo "Arguments:"
            echo "  frames_folder              Path to folder containing PNG frames"
            echo ""
            echo "Options:"
            echo "  --fps, -f FPS              Frame rate (default: 25)"
            echo "  --output, -o FILE          Output video file (default: <frames_folder>.mov)"
            echo "  --pattern, -p PATTERN      Frame filename pattern (default: frame_%04d.png)"
            echo "                             Use ffmpeg pattern syntax (e.g., frame_%04d.png, img_%03d.png)"
            echo "  --help, -h                 Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0 /path/to/frames"
            echo "  $0 /path/to/frames --fps 30 --output output.mov"
            echo "  $0 /path/to/frames --pattern 'img_%03d.png'"
            exit 0
            ;;
        *)
            if [ -z "$FRAMES_FOLDER" ]; then
                FRAMES_FOLDER="$1"
            else
                echo "Error: Unexpected argument: $1"
                echo "Use --help for usage information"
                exit 1
            fi
            shift
            ;;
    esac
done

# Check if frames folder is provided
if [ -z "$FRAMES_FOLDER" ]; then
    echo "Error: frames_folder is required"
    echo "Use --help for usage information"
    exit 1
fi

# Check if frames folder exists
if [ ! -d "$FRAMES_FOLDER" ]; then
    echo "Error: Frames folder does not exist: $FRAMES_FOLDER"
    exit 1
fi

# Set default output if not provided
if [ -z "$OUTPUT_VIDEO" ]; then
    # Use folder name as base for output file
    FOLDER_NAME=$(basename "$FRAMES_FOLDER")
    OUTPUT_VIDEO="${FOLDER_NAME}.mov"
fi

# Check if ffmpeg is available
if ! command -v ffmpeg >/dev/null 2>&1; then
    # Try common ffmpeg paths
    if [ -f "/opt/ffmpeg/bin/ffmpeg" ]; then
        FFMPEG="/opt/ffmpeg/bin/ffmpeg"
    else
        echo "Error: ffmpeg not found in PATH or /opt/ffmpeg/bin/ffmpeg"
        exit 1
    fi
else
    FFMPEG="ffmpeg"
fi

# Auto-detect frame pattern if not explicitly provided
if [ "$PATTERN" == "frame_%04d.png" ]; then
    # Find first PNG file to detect naming pattern
    FIRST_FRAME=$(find "$FRAMES_FOLDER" -maxdepth 1 -name "*.png" 2>/dev/null | sort | head -1)
    if [ -n "$FIRST_FRAME" ]; then
        FRAME_NAME=$(basename "$FIRST_FRAME")
        # Try to detect pattern: frame_00000.png -> frame_%05d.png
        if [[ "$FRAME_NAME" =~ ^frame_([0-9]+)\.png$ ]]; then
            DIGIT_COUNT=${#BASH_REMATCH[1]}
            PATTERN="frame_%0${DIGIT_COUNT}d.png"
            echo "[FRAMES_TO_MOV] Auto-detected frame pattern: $PATTERN (from $FRAME_NAME)"
        fi
    fi
fi

# Build input pattern path
INPUT_PATTERN="${FRAMES_FOLDER}/${PATTERN}"

# Check if any frames match the pattern
# Try to find at least one frame file
FRAME_COUNT=$(find "$FRAMES_FOLDER" -maxdepth 1 -name "*.png" 2>/dev/null | wc -l | tr -d ' ')
if [ "$FRAME_COUNT" -eq 0 ]; then
    echo "Error: No PNG frames found in $FRAMES_FOLDER"
    exit 1
fi

echo "[FRAMES_TO_MOV] Converting frames to ProRes 4444 transparent MOV..."
echo "[FRAMES_TO_MOV] Frames folder: $FRAMES_FOLDER"
echo "[FRAMES_TO_MOV] Frame pattern: $PATTERN"
echo "[FRAMES_TO_MOV] Frame rate: $FPS"
echo "[FRAMES_TO_MOV] Output video: $OUTPUT_VIDEO"
echo "[FRAMES_TO_MOV] Found $FRAME_COUNT PNG frame(s)"

# Convert frames to ProRes 4444 transparent MOV
# Use ProRes 4444 codec for transparent MOV with alpha channel support
"$FFMPEG" -y \
    -framerate "$FPS" \
    -i "$INPUT_PATTERN" \
    -c:v prores_ks \
    -profile:v 4444 \
    -pix_fmt yuva444p10le \
    "$OUTPUT_VIDEO"

# Check if video was created
if [ -f "$OUTPUT_VIDEO" ]; then
    VIDEO_SIZE=$(du -h "$OUTPUT_VIDEO" | cut -f1)
    echo "[FRAMES_TO_MOV] Video created successfully: $OUTPUT_VIDEO"
    echo "[FRAMES_TO_MOV] Video size: $VIDEO_SIZE"
    exit 0
else
    echo "[FRAMES_TO_MOV] Error: Video file was not created"
    exit 1
fi

