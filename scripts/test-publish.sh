#!/bin/bash
set -e

echo "🔍 Testing package structure..."
cd test-package

# Check package.json
echo "📦 Package info:"
cat package.json | grep -E '"name"|"version"|"main"'

# Verify files exist
echo ""
echo "📁 Checking files:"
test -f browser/index.js && echo "✅ browser/index.js" || echo "❌ browser/index.js missing"
test -f browser/wasm.js && echo "✅ browser/wasm.js" || echo "❌ browser/wasm.js missing"
test -f browser/lotio.wasm && echo "✅ browser/lotio.wasm" || echo "❌ browser/lotio.wasm missing"
test -f browser/lotio.js && echo "✅ browser/lotio.js" || echo "❌ browser/lotio.js missing"
test -f README.md && echo "✅ README.md" || echo "❌ README.md missing"

# Test package creation
echo ""
echo "📦 Creating test package..."
npm pack --dry-run

echo ""
echo "✅ Package structure looks good!"
echo ""
echo "To publish to GitHub Packages, run:"
echo "  cd test-package"
echo "  npm publish"
echo ""
echo "Make sure you have configured npm for GitHub Packages:"
echo "  echo '@matrunchyk:registry=https://npm.pkg.github.com' >> ~/.npmrc"
echo "  echo '//npm.pkg.github.com/:_authToken=YOUR_TOKEN' >> ~/.npmrc"

