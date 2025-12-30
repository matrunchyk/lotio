#!/bin/bash

# Quick script to check Skia build status

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia/skia"
BUILD_DIR="$SKIA_ROOT/out/Release"

echo "🔍 Checking Skia build status..."
echo ""

# Check if ninja is running
if pgrep -f "ninja.*Release" > /dev/null; then
    echo "✅ Build is RUNNING (ninja process found)"
    NINJA_PID=$(pgrep -f "ninja.*Release" | head -1)
    echo "   Process ID: $NINJA_PID"
    echo "   CPU usage: $(ps -p $NINJA_PID -o %cpu= 2>/dev/null | tr -d ' ')%"
else
    echo "⏸️  Build is NOT running (no ninja process found)"
    echo "   💡 To restart: cd third_party/skia/skia && ninja -C out/Release"
fi

echo ""

# Check build log
if [ -f "$BUILD_DIR/.ninja_log" ]; then
    TOTAL_TARGETS=$(wc -l < "$BUILD_DIR/.ninja_log" 2>/dev/null || echo "0")
    echo "📊 Build Progress:"
    echo "   Targets completed: $TOTAL_TARGETS"
    echo "   Estimated total: ~1187"
    
    if [ "$TOTAL_TARGETS" -gt 0 ]; then
        PERCENT=$((TOTAL_TARGETS * 100 / 1187))
        echo "   Progress: ~${PERCENT}%"
    fi
    
    echo ""
    echo "📝 Last build activity:"
    tail -1 "$BUILD_DIR/.ninja_log" 2>/dev/null | awk '{print "   " $0}'
else
    echo "⚠️  Build log not found - build may not have started"
fi

echo ""

# Check for output files
if [ -f "$BUILD_DIR/libskia.a" ] || [ -f "$BUILD_DIR/libskia.dylib" ]; then
    echo "✅ Skia libraries found - Build COMPLETE!"
    ls -lh "$BUILD_DIR"/libskia.* 2>/dev/null | awk '{print "   " $9 " (" $5 ")"}'
else
    echo "⏳ Skia libraries not found yet - still building..."
    
    # Count object files as progress indicator
    OBJ_COUNT=$(find "$BUILD_DIR" -name "*.o" 2>/dev/null | wc -l | tr -d ' ')
    if [ "$OBJ_COUNT" -gt 0 ]; then
        echo "   Object files compiled: $OBJ_COUNT"
    fi
fi

echo ""
echo "💡 To monitor in real-time:"
echo "   watch -n 2 './check_build.sh'"
echo "   # or"
echo "   tail -f $BUILD_DIR/.ninja_log"

