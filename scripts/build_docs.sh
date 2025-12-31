#!/bin/bash
# Build documentation for GitHub Pages
# Converts markdown files to HTML with sidebar navigation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DOCS_DIR="$PROJECT_ROOT/docs"
BUILD_DIR="$PROJECT_ROOT/_site"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Building documentation...${NC}"

# Create build directory
mkdir -p "$BUILD_DIR"

# Copy template
TEMPLATE="$DOCS_DIR/_template.html"

if [ ! -f "$TEMPLATE" ]; then
    echo "Error: Template not found: $TEMPLATE"
    exit 1
fi

# Function to convert markdown to HTML (simple implementation)
# Note: This is a basic converter. For production, consider using a proper markdown parser.
convert_markdown() {
    local md_file="$1"
    local html_file="$2"
    local title="$3"
    local active_page="$4"
    
    # Read markdown content
    local content=$(cat "$md_file")
    
    # Simple markdown to HTML conversion
    # Headers
    content=$(echo "$content" | sed 's/^# \(.*\)$/<h1>\1<\/h1>/')
    content=$(echo "$content" | sed 's/^## \(.*\)$/<h2>\1<\/h2>/')
    content=$(echo "$content" | sed 's/^### \(.*\)$/<h3>\1<\/h3>/')
    content=$(echo "$content" | sed 's/^#### \(.*\)$/<h4>\1<\/h4>/')
    
    # Code blocks (simple - assumes ```language blocks)
    content=$(echo "$content" | awk '
        /^```/ {
            if (in_code) {
                print "</pre>"
                in_code = 0
            } else {
                print "<pre><code>"
                in_code = 1
            }
            next
        }
        in_code {
            print
            next
        }
        {
            # Inline code
            gsub(/`([^`]+)`/, "<code>\\1</code>", $0)
            print
        }
    ')
    
    # Bold
    content=$(echo "$content" | sed 's/\*\*\([^*]*\)\*\*/<strong>\1<\/strong>/g')
    
    # Links
    content=$(echo "$content" | sed 's/\[\([^\]]*\)\](\([^)]*\))/<a href="\2">\1<\/a>/g')
    
    # Lists (simple - assumes * or -)
    content=$(echo "$content" | sed 's/^\* \(.*\)$/<li>\1<\/li>/')
    content=$(echo "$content" | sed 's/^- \(.*\)$/<li>\1<\/li>/')
    
    # Paragraphs (lines that don't start with HTML tags)
    content=$(echo "$content" | awk '
        /^<[\/]?[h|p|u|o|d|s]/ || /^<li>/ || /^<pre>/ || /^<\/pre>/ || /^<code>/ || /^<\/code>/ || /^$/ {
            print
            next
        }
        {
            print "<p>" $0 "</p>"
        }
    ')
    
    # Wrap list items in ul tags (simplified)
    content=$(echo "$content" | awk '
        BEGIN { in_list = 0 }
        /^<li>/ {
            if (!in_list) {
                print "<ul>"
                in_list = 1
            }
            print
            next
        }
        {
            if (in_list && !/^<li>/) {
                print "</ul>"
                in_list = 0
            }
            print
        }
        END {
            if (in_list) {
                print "</ul>"
            }
        }
    ')
    
    # Load template
    local html=$(cat "$TEMPLATE")
    
    # Replace placeholders
    html=$(echo "$html" | sed "s/{{TITLE}}/$title/g")
    html=$(echo "$html" | sed "s/{{CONTENT}}/$content/g")
    
    # Set active page
    case "$active_page" in
        overview)
            html=$(echo "$html" | sed 's/{{OVERVIEW_ACTIVE}}/class="active"/g')
            html=$(echo "$html" | sed 's/{{CLI_ACTIVE}}//g')
            html=$(echo "$html" | sed 's/{{JS_ACTIVE}}//g')
            html=$(echo "$html" | sed 's/{{CPP_ACTIVE}}//g')
            ;;
        cli)
            html=$(echo "$html" | sed 's/{{OVERVIEW_ACTIVE}}//g')
            html=$(echo "$html" | sed 's/{{CLI_ACTIVE}}/class="active"/g')
            html=$(echo "$html" | sed 's/{{JS_ACTIVE}}//g')
            html=$(echo "$html" | sed 's/{{CPP_ACTIVE}}//g')
            ;;
        js-library)
            html=$(echo "$html" | sed 's/{{OVERVIEW_ACTIVE}}//g')
            html=$(echo "$html" | sed 's/{{CLI_ACTIVE}}//g')
            html=$(echo "$html" | sed 's/{{JS_ACTIVE}}/class="active"/g')
            html=$(echo "$html" | sed 's/{{CPP_ACTIVE}}//g')
            ;;
        cpp-library)
            html=$(echo "$html" | sed 's/{{OVERVIEW_ACTIVE}}//g')
            html=$(echo "$html" | sed 's/{{CLI_ACTIVE}}//g')
            html=$(echo "$html" | sed 's/{{JS_ACTIVE}}//g')
            html=$(echo "$html" | sed 's/{{CPP_ACTIVE}}/class="active"/g')
            ;;
    esac
    
    # Write HTML file
    echo "$html" > "$html_file"
}

# Use a proper markdown converter if available
if command -v pandoc &> /dev/null; then
    echo -e "${GREEN}Using pandoc for markdown conversion${NC}"
    
    # Convert each markdown file
    pandoc "$DOCS_DIR/overview.md" -f markdown -t html -o "$BUILD_DIR/overview.html" --standalone --template="$TEMPLATE" || {
        echo "Warning: pandoc conversion failed, using simple converter"
        convert_markdown "$DOCS_DIR/overview.md" "$BUILD_DIR/overview.html" "Overview" "overview"
    }
    
    # For now, use simple converter for all files
    # We'll enhance this later
    convert_markdown "$DOCS_DIR/overview.md" "$BUILD_DIR/index.html" "Overview" "overview"
    convert_markdown "$DOCS_DIR/cli.md" "$BUILD_DIR/cli.html" "CLI" "cli"
    convert_markdown "$DOCS_DIR/js-library.md" "$BUILD_DIR/js-library.html" "JS Library" "js-library"
    convert_markdown "$DOCS_DIR/cpp-library.md" "$BUILD_DIR/cpp-library.html" "C++ Library" "cpp-library"
else
    echo -e "${BLUE}Using simple markdown converter${NC}"
    echo -e "${BLUE}Install pandoc for better markdown support: brew install pandoc${NC}"
    
    # Use simple converter
    convert_markdown "$DOCS_DIR/overview.md" "$BUILD_DIR/index.html" "Overview" "overview"
    convert_markdown "$DOCS_DIR/overview.md" "$BUILD_DIR/overview.html" "Overview" "overview"
    convert_markdown "$DOCS_DIR/cli.md" "$BUILD_DIR/cli.html" "CLI" "cli"
    convert_markdown "$DOCS_DIR/js-library.md" "$BUILD_DIR/js-library.html" "JS Library" "js-library"
    convert_markdown "$DOCS_DIR/cpp-library.md" "$BUILD_DIR/cpp-library.html" "C++ Library" "cpp-library"
fi

# Copy other assets
echo -e "${BLUE}Copying assets...${NC}"
cp -r "$PROJECT_ROOT/browser" "$BUILD_DIR/" 2>/dev/null || true
cp -r "$PROJECT_ROOT/examples/samples" "$BUILD_DIR/" 2>/dev/null || true
cp -r "$PROJECT_ROOT/examples/fonts" "$BUILD_DIR/" 2>/dev/null || true

echo -e "${GREEN}Documentation built successfully!${NC}"
echo -e "${GREEN}Output: $BUILD_DIR${NC}"

