#!/bin/bash
set -e

# This script updates the Homebrew formula in the tap repository
# It updates: url, sha256, version, and bottle SHA256

VERSION="$1"
VERSION_NUMBER="${VERSION#v}"  # Remove 'v' prefix
SOURCE_SHA256="$2"
BOTTLE_SHA256="$3"
BOTTLE_ARCH="${4:-arm64_big_sur}"

if [ -z "$VERSION" ] || [ -z "$SOURCE_SHA256" ] || [ -z "$BOTTLE_SHA256" ]; then
  echo "Usage: $0 <VERSION> <SOURCE_SHA256> <BOTTLE_SHA256> [BOTTLE_ARCH]"
  echo "Example: $0 v20251230-942682b abc123... def456... arm64_big_sur"
  exit 1
fi

TAP_REPO="matrunchyk/homebrew-lotio"
FORMULA_PATH="Formula/lotio.rb"
TEMP_DIR=$(mktemp -d)

echo "Cloning tap repository..."
# Use token for authentication if provided
if [ -n "$HOMEBREW_TAP_TOKEN" ]; then
  git clone "https://x-access-token:${HOMEBREW_TAP_TOKEN}@github.com/${TAP_REPO}.git" "$TEMP_DIR"
else
  git clone "https://github.com/${TAP_REPO}.git" "$TEMP_DIR"
fi
cd "$TEMP_DIR"

# Update the formula
echo "Updating formula..."
sed -i.bak \
  -e "s|url \".*\"|url \"https://github.com/matrunchyk/lotio/archive/refs/tags/${VERSION}.tar.gz\"|" \
  -e "s|sha256 \".*\"|sha256 \"${SOURCE_SHA256}\"|" \
  -e "s|version \".*\"|version \"${VERSION_NUMBER}\"|" \
  -e "s|root_url \".*\"|root_url \"https://github.com/matrunchyk/lotio/releases/download/${VERSION}\"|" \
  -e "s|sha256 ${BOTTLE_ARCH}: \".*\"|sha256 ${BOTTLE_ARCH}: \"${BOTTLE_SHA256}\"|" \
  "$FORMULA_PATH"

# Remove backup file
rm -f "${FORMULA_PATH}.bak"

# Commit and push
echo "Committing changes..."
git config user.name "github-actions[bot]"
git config user.email "github-actions[bot]@users.noreply.github.com"
git add "$FORMULA_PATH"

# Check if there are changes to commit
if git diff --staged --quiet; then
  echo "No changes to commit (formula already up-to-date)"
else
  git commit -m "Update lotio to ${VERSION}"
  # Push using token if provided
  if [ -n "$HOMEBREW_TAP_TOKEN" ]; then
    git remote set-url origin "https://x-access-token:${HOMEBREW_TAP_TOKEN}@github.com/${TAP_REPO}.git"
  fi
  git push origin main
fi

echo "✅ Formula updated successfully!"
cd -
rm -rf "$TEMP_DIR"

