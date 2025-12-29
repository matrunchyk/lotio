#!/bin/bash
set -e

# This script replaces the broken tools/git-sync-deps
# It parses the DEPS file and clones all required externals

SKIA_ROOT="${1:-$(pwd)}"

if [ ! -f "$SKIA_ROOT/DEPS" ]; then
    echo "Error: DEPS file not found at $SKIA_ROOT/DEPS"
    echo "Usage: $0 [skia_root_directory]"
    exit 1
fi

cd "$SKIA_ROOT"

# Suppress detached HEAD warnings for CI/CD environments
git config --global advice.detachedHead false

echo "Syncing Skia dependencies from DEPS file..."
echo "Creating third_party/externals directory..."
mkdir -p third_party/externals

# Extract all externals from DEPS file
# Format: "third_party/externals/name": "url@commit",
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
    
    # Skip if already exists
    if [ -d "third_party/externals/$name" ]; then
        echo "  ✓ $name already exists, skipping"
        continue
    fi
    
    # Clone with retries
    echo "  Cloning $name from $url (commit: $commit)..."
    cd "$SKIA_ROOT/third_party/externals"
    
    success=false
    for attempt in 1 2 3; do
        if git clone "$url" "$name" 2>&1; then
            success=true
            break
        else
            if [ $attempt -lt 3 ]; then
                echo "    Attempt $attempt failed, retrying in 5 seconds..."
                sleep 5
            fi
        fi
    done
    
    if [ "$success" = true ] && [ -d "$name" ]; then
        cd "$name"
        if git checkout "$commit" 2>&1; then
            echo "  ✓ $name cloned and checked out to $commit"
        else
            echo "    Warning: Failed to checkout $commit for $name, using default branch"
        fi
    else
        echo "  ✗ Failed to clone $name after 3 attempts"
        exit 1
    fi
    
    # Always return to SKIA_ROOT
    cd "$SKIA_ROOT"
done

echo "✅ Done syncing Skia dependencies"

