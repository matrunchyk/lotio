#!/bin/bash
set -e

BUILD_ROOT="/skia-build"
SKIA_ROOT="$BUILD_ROOT/skia"

echo "Cloning Skia repository..."
cd "$BUILD_ROOT"
git clone https://skia.googlesource.com/skia.git
echo "Skia repository cloned"

echo "Creating third_party/externals directory..."
cd "$SKIA_ROOT"
mkdir -p third_party/externals
echo "third_party/externals directory created"

# Extract all externals from DEPS file
grep '"third_party/externals/' DEPS | while IFS= read -r line; do
    # Extract name, url, and commit
    name=$(echo "$line" | sed -n 's/.*"third_party\/externals\/\([^"]*\)".*/\1/p')
    url_commit=$(echo "$line" | sed -n 's/.*: "\([^"]*\)".*/\1/p')
    
    if [ -z "$name" ] || [ -z "$url_commit" ]; then
        continue
    fi
    
    # Split URL and commit (separated by @)
    url=$(echo "$url_commit" | sed 's/@.*//')
    commit=$(echo "$url_commit" | sed 's/.*@//')
    
    echo "Processing: $name"
    
    # Always work from SKIA_ROOT
    cd "$SKIA_ROOT"
    
    # Skip if already exists
    if [ -d "third_party/externals/$name" ]; then
        echo "  ✓ $name already exists, skipping"
        continue
    fi
    
    # Clone with retries
    echo "  Cloning $name from $url..."
    cd "$SKIA_ROOT/third_party/externals"
    
    for attempt in 1 2 3; do
        if git clone "$url" "$name" 2>&1; then
            break
        else
            echo "    Attempt $attempt failed, retrying in 5 seconds..."
            sleep 5
        fi
    done
    
    if [ -d "$name" ]; then
        cd "$name"
        git checkout "$commit" 2>&1 || echo "    Warning: Failed to checkout $commit"
        echo "  ✓ $name cloned"
    else
        echo "  ✗ Failed to clone $name after 3 attempts"
    fi
    
    # Always return to SKIA_ROOT
    cd "$SKIA_ROOT"
done

echo "Done cloning externals"
