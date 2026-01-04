#!/usr/bin/env python3
"""
Build documentation for GitHub Pages
Converts markdown files to HTML with sidebar navigation
"""

import os
import sys
import re
from pathlib import Path

try:
    import markdown
    from markdown.extensions import codehilite, fenced_code, tables
    HAS_MARKDOWN = True
except ImportError:
    HAS_MARKDOWN = False
    print("Warning: markdown library not found. Install with: pip install markdown")
    print("Using basic markdown conversion...")

def load_template(template_path):
    """Load HTML template"""
    with open(template_path, 'r', encoding='utf-8') as f:
        return f.read()

def wrap_code_lines_in_paragraphs(code_content):
    """Wrap each line of code content in <p> tags"""
    lines = code_content.split('\n')
    wrapped_lines = []
    for line in lines:
        if line.strip():  # Only wrap non-empty lines
            wrapped_lines.append(f'<p>{line}</p>')
        else:
            wrapped_lines.append(line)  # Keep empty lines as-is
    return '\n'.join(wrapped_lines)

def add_language_classes_to_pre(html):
    """Post-process HTML to add language classes to <pre> tags based on <code> classes
    and wrap each line in <p> tags"""
    # Pattern: <pre><code class="language-xxx">...</code></pre>
    # Convert to: <pre class="xxx"><code class="language-xxx">...</code></pre>
    def replace_pre(match):
        pre_tag = match.group(1)
        code_tag = match.group(2)
        code_content = match.group(3)
        closing_tags = match.group(4)
        
        # Wrap each line in <p> tags
        wrapped_content = wrap_code_lines_in_paragraphs(code_content)
        
        # Extract language from code tag class
        lang_match = re.search(r'class="language-(\w+)"', code_tag)
        if lang_match:
            lang = lang_match.group(1)
            # Add class to pre tag
            if 'class=' in pre_tag:
                # Replace existing class
                pre_tag = re.sub(r'class="([^"]*)"', f'class="\\1 {lang}"', pre_tag)
            else:
                # Add new class attribute
                pre_tag = pre_tag.rstrip('>') + f' class="{lang}">'
            return f'{pre_tag}{code_tag}{wrapped_content}{closing_tags}'
        return match.group(0)
    
    # Match <pre> followed by <code> with optional language class
    pattern = r'(<pre[^>]*>)(<code[^>]*>)(.*?)(</code></pre>)'
    html = re.sub(pattern, replace_pre, html, flags=re.DOTALL)
    
    return html

def convert_markdown_to_html(md_content):
    """Convert markdown to HTML"""
    # Protect script tags and their content from markdown processing
    # Use HTML comment format to avoid markdown processing
    script_blocks = []
    def protect_script(match):
        placeholder = f'<!-- SCRIPT_BLOCK_{len(script_blocks)} -->'
        script_blocks.append(match.group(0))  # Store entire script tag with content
        return placeholder
    
    # Extract all script tags (including their content) before processing
    script_pattern = r'(<script[^>]*>.*?</script>)'
    md_content = re.sub(script_pattern, protect_script, md_content, flags=re.DOTALL | re.IGNORECASE)
    
    if HAS_MARKDOWN:
        # Markdown preserves HTML by default, which we want for script tags
        md = markdown.Markdown(
            extensions=['codehilite', 'fenced_code', 'tables', 'nl2br'],
            extension_configs={
                'codehilite': {
                    'use_pygments': False
                }
            }
        )
        html = md.convert(md_content)
        # Post-process to add language classes to <pre> tags
        html = add_language_classes_to_pre(html)
        
        # Restore script blocks (unchanged)
        for i, script_block in enumerate(script_blocks):
            placeholder = f'<!-- SCRIPT_BLOCK_{i} -->'
            html = html.replace(placeholder, script_block)
        
        return html
    else:
        # Basic fallback conversion
        html = md_content
        
        # Script blocks are already protected above, now protect code blocks
        code_blocks = []
        def protect_code_block(match):
            placeholder = f'__CODE_BLOCK_{len(code_blocks)}__'
            # Store the language and content for later processing
            lang = match.group(1) or ''
            code_content = match.group(2)
            code_blocks.append((lang, code_content))
            return placeholder
        
        html = re.sub(r'```(\w+)?\n(.*?)```', protect_code_block, html, flags=re.DOTALL)
        
        # Headers (now safe to process, code blocks are protected)
        html = re.sub(r'^# (.+)$', r'<h1>\1</h1>', html, flags=re.MULTILINE)
        html = re.sub(r'^## (.+)$', r'<h2>\1</h2>', html, flags=re.MULTILINE)
        html = re.sub(r'^### (.+)$', r'<h3>\1</h3>', html, flags=re.MULTILINE)
        html = re.sub(r'^#### (.+)$', r'<h4>\1</h4>', html, flags=re.MULTILINE)
        
        # Restore and process code blocks - wrap each line in <p> tags
        for i, (lang, code_content) in enumerate(code_blocks):
            placeholder = f'__CODE_BLOCK_{i}__'
            # Wrap each line in <p> tags
            wrapped_content = wrap_code_lines_in_paragraphs(code_content)
            if lang:
                processed = f'<pre class="{lang}"><code>{wrapped_content}</code></pre>'
            else:
                processed = f'<pre><code>{wrapped_content}</code></pre>'
            html = html.replace(placeholder, processed)
        
        # Inline code
        html = re.sub(r'`([^`]+)`', r'<code>\1</code>', html)
        
        # Bold
        html = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', html)
        
        # Links
        html = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', html)
        
        # Lists and paragraph wrapping
        # Protect <pre> blocks by replacing them with placeholders
        pre_blocks = []
        def replace_pre(match):
            placeholder = f'__PRE_BLOCK_{len(pre_blocks)}__'
            pre_blocks.append(match.group(0))
            return f'\n{placeholder}\n'  # Add newlines to ensure it's on its own line(s)
        
        html = re.sub(r'<pre[^>]*>.*?</pre>', replace_pre, html, flags=re.DOTALL)
        
        # Now process the rest for lists and paragraphs
        lines = html.split('\n')
        in_ul = False  # Unordered list
        in_ol = False  # Ordered list
        ol_indent = 0  # Indentation level of current ordered list
        ul_indent = 0  # Indentation level of current unordered list
        open_li = False  # Track if we have an open <li> that might contain nested lists
        result = []
        
        def has_nested_list_ahead(lines, current_idx, parent_indent):
            """Check if there's a nested list item in the next few non-empty lines"""
            for i in range(current_idx + 1, min(current_idx + 5, len(lines))):
                line = lines[i]
                stripped = line.strip()
                if not stripped:
                    continue
                # Check if it's a list item with more indentation
                indent_match = re.match(r'^(\s*)', line)
                line_indent = len(indent_match.group(1)) if indent_match else 0
                if (re.match(r'^\s*[\*\-\+] ', line) or re.match(r'^\s*\d+\. ', line)) and line_indent > parent_indent:
                    return True
                # If we hit a same-level or parent-level list item, no nested list
                if (re.match(r'^\s*[\*\-\+] ', line) or re.match(r'^\s*\d+\. ', line)) and line_indent <= parent_indent:
                    return False
                # If we hit other content, assume no nested list
                if stripped and not re.match(r'^\s*[\*\-\+] ', line) and not re.match(r'^\s*\d+\. ', line):
                    return False
            return False
        
        for idx, line in enumerate(lines):
            stripped = line.strip()
            # Check if this line is a placeholder (exact match after stripping)
            is_placeholder = any(stripped == f'__PRE_BLOCK_{i}__' for i in range(len(pre_blocks)))
            
            if is_placeholder:
                # Don't wrap placeholders - just add them as-is
                result.append(line)
            elif re.match(r'^\s*[\*\-\+] ', line):
                # Unordered list item (allow leading whitespace for indented lists)
                # Get indentation level (number of leading spaces)
                indent_match = re.match(r'^(\s*)', line)
                current_indent = len(indent_match.group(1)) if indent_match else 0
                
                # Check if this is a nested sub-list within an ordered list
                if in_ol and current_indent > ol_indent:
                    # This is a nested unordered list inside an ordered list item
                    # If we have an open <li>, the nested <ul> goes inside it
                    if open_li:
                        # The <li> is already open, just start the nested <ul>
                        if not in_ul:
                            result.append('<ul>')
                            in_ul = True
                        ul_indent = current_indent
                    else:
                        # Shouldn't happen, but handle it
                        if not in_ul:
                            result.append('<ul>')
                            in_ul = True
                        ul_indent = current_indent
                else:
                    # Regular unordered list (not nested)
                    # Close any open <li> from ordered list
                    if open_li:
                        open_li = False
                    if in_ol and current_indent <= ol_indent:
                        # Close ordered list if we're going back to same or less indentation
                        result.append('</ol>')
                        in_ol = False
                        ol_indent = 0
                    if not in_ul:
                        result.append('<ul>')
                        in_ul = True
                    ul_indent = current_indent
                
                # Remove leading whitespace and list marker, keep the rest
                list_content = re.sub(r'^\s*[\*\-\+] ', '', line)
                result.append('<li>' + list_content + '</li>')
            elif re.match(r'^\s*\d+\. ', line):
                # Ordered list item (e.g., "1. ", "2. ", etc.) - allow leading whitespace
                # Get indentation level
                indent_match = re.match(r'^(\s*)', line)
                current_indent = len(indent_match.group(1)) if indent_match else 0
                
                # Close previous <li> if we have one open
                if open_li:
                    # Close any nested unordered list first
                    if in_ul:
                        result.append('</ul>')
                        in_ul = False
                        ul_indent = 0
                    result.append('</li>')
                    open_li = False
                
                # If we have an open unordered list and this ordered item has same/less indent, close ul
                if in_ul and current_indent <= ul_indent:
                    result.append('</ul>')
                    in_ul = False
                    ul_indent = 0
                
                # Continue or start ordered list
                if not in_ol:
                    result.append('<ol>')
                    in_ol = True
                    ol_indent = current_indent
                elif current_indent < ol_indent:
                    # Less indentation means we're going back to a parent level
                    # This shouldn't happen in normal markdown, but handle it
                    result.append('</ol>')
                    result.append('<ol>')
                    ol_indent = current_indent
                
                # Remove leading whitespace, number and dot, keep the rest
                list_content = re.sub(r'^\s*\d+\. ', '', line)
                
                # Check if there's a nested list coming up
                if has_nested_list_ahead(lines, idx, current_indent):
                    # Keep <li> open for nested content
                    result.append('<li>' + list_content)
                    open_li = True
                else:
                    # Close <li> immediately
                    result.append('<li>' + list_content + '</li>')
                    open_li = False
            else:
                # Not a list item
                # Check if this is an empty line or content that should close nested lists
                if stripped == '':
                    # Empty line - keep lists open (might be spacing)
                    result.append(line)
                else:
                    # Non-empty non-list line - close nested unordered lists but keep ordered list open
                    # (in case there's more content in the list item)
                    if in_ul and (not in_ol or stripped.startswith('<')):  # Close ul if not nested in ol
                        # Check if this looks like it should close the list
                        # If it's HTML or starts a new section, close the list
                        if re.match(r'^<[hH]', stripped) or re.match(r'^<[^>]+>', stripped):
                            result.append('</ul>')
                            in_ul = False
                            ul_indent = 0
                    
                    # Close ordered list if we hit a header or other block element
                    if in_ol and (re.match(r'^<[hH]', stripped) or (stripped.startswith('<') and not in_ul)):
                        result.append('</ol>')
                        in_ol = False
                        ol_indent = 0
                    
                    # Preserve HTML tags (like <script>, <div>, etc.)
                    if stripped and not (stripped.startswith('<') and stripped.endswith('>')):
                        # Only wrap in <p> if it's not already HTML
                        if not re.match(r'^<[^>]+>', stripped):
                            result.append('<p>' + line + '</p>')
                        else:
                            result.append(line)
                    else:
                        result.append(line)
        # Close any open tags
        if open_li:
            if in_ul:
                result.append('</ul>')
            result.append('</li>')
        if in_ul:
            result.append('</ul>')
        if in_ol:
            result.append('</ol>')
        html = '\n'.join(result)
        
        # Restore <pre> blocks - remove any <p> tags that might have been added around placeholders
        for i, pre_block in enumerate(pre_blocks):
            placeholder = f'__PRE_BLOCK_{i}__'
            # Remove <p> tags that might wrap the placeholder
            html = re.sub(r'<p>\s*' + re.escape(placeholder) + r'\s*</p>', placeholder, html, flags=re.MULTILINE)
            html = re.sub(r'<p>\s*' + re.escape(placeholder) + r'\s*</p>', placeholder, html)  # Try without MULTILINE too
            # Now restore the actual block
            html = html.replace(placeholder, pre_block)
        
        # Restore script blocks (unchanged)
        for i, script_block in enumerate(script_blocks):
            placeholder = f'<!-- SCRIPT_BLOCK_{i} -->'
            # Remove any <p> tags that might wrap the placeholder
            html = re.sub(r'<p>\s*' + re.escape(placeholder) + r'\s*</p>', placeholder, html, flags=re.MULTILINE)
            html = re.sub(r'<p>\s*' + re.escape(placeholder) + r'\s*</p>', placeholder, html)
            # Restore the actual script block
            html = html.replace(placeholder, script_block)
        
        return html

def build_docs():
    """Build all documentation pages"""
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    docs_dir = project_root / "docs"
    build_dir = project_root / "_site"
    template_path = docs_dir / "_template.html"
    
    # Create build directory
    build_dir.mkdir(exist_ok=True)
    
    # Load template
    template = load_template(template_path)
    
    # Documentation pages
    pages = [
        ("overview.md", "index.html", "Overview", "overview"),
        ("overview.md", "overview.html", "Overview", "overview"),
        ("cli.md", "cli.html", "CLI", "cli"),
        ("docker.md", "docker.html", "Docker", "docker"),
        ("js-library.md", "js-library.html", "JS Library", "js-library"),
        ("cpp-library.md", "cpp-library.html", "C++ Library", "cpp-library"),
    ]
    
    for md_file, html_file, title, active_page in pages:
        md_path = docs_dir / md_file
        html_path = build_dir / html_file
        
        if not md_path.exists():
            print(f"Warning: {md_path} not found, skipping...")
            continue
        
        # Read markdown
        with open(md_path, 'r', encoding='utf-8') as f:
            md_content = f.read()
        
        # Convert to HTML
        html_content = convert_markdown_to_html(md_content)
        
        # Get version from environment or git tag
        version = os.environ.get('VERSION', 'dev')
        if version == 'dev':
            # Try to get from git tag
            try:
                import subprocess
                result = subprocess.run(
                    ['git', 'describe', '--tags', '--match', 'v*.*.*', '--abbrev=0'],
                    capture_output=True,
                    text=True,
                    cwd=project_root
                )
                if result.returncode == 0:
                    version = result.stdout.strip()
            except:
                pass
        
        # Replace template placeholders
        html = template
        html = html.replace("{{TITLE}}", title)
        html = html.replace("{{CONTENT}}", html_content)
        html = html.replace("{{VERSION}}", version)
        
        # Set active page
        active_classes = {
            "overview": ("class=\"active\"", "", "", "", ""),
            "cli": ("", "class=\"active\"", "", "", ""),
            "docker": ("", "", "class=\"active\"", "", ""),
            "js-library": ("", "", "", "class=\"active\"", ""),
            "cpp-library": ("", "", "", "", "class=\"active\""),
        }
        
        overview_active, cli_active, docker_active, js_active, cpp_active = active_classes.get(active_page, ("", "", "", "", ""))
        html = html.replace("{{OVERVIEW_ACTIVE}}", overview_active)
        html = html.replace("{{CLI_ACTIVE}}", cli_active)
        html = html.replace("{{DOCKER_ACTIVE}}", docker_active)
        html = html.replace("{{JS_ACTIVE}}", js_active)
        html = html.replace("{{CPP_ACTIVE}}", cpp_active)
        
        # Write HTML file
        with open(html_path, 'w', encoding='utf-8') as f:
            f.write(html)
        
        print(f"Built: {html_file}")
    
    # Copy assets
    print("Copying assets...")
    import shutil
    
    # Copy browser files from npm package
    npm_browser = project_root / "node_modules" / "@matrunchyk" / "lotio" / "browser"
    local_browser = build_dir / "browser"
    if npm_browser.exists():
        if local_browser.exists():
            shutil.rmtree(local_browser)
        shutil.copytree(npm_browser, local_browser)
        print(f"Copied: browser (from npm package)")
    else:
        # Fallback to local browser directory if npm package not found
        local_browser_src = project_root / "browser"
        if local_browser_src.exists():
            if local_browser.exists():
                shutil.rmtree(local_browser)
            shutil.copytree(local_browser_src, local_browser)
            print(f"Copied: browser (from local directory)")
        else:
            print(f"Warning: browser directory not found in npm package or locally")
    
    # Copy examples
    for asset_dir in ["examples/samples", "examples/fonts"]:
        src = project_root / asset_dir
        dst = build_dir / Path(asset_dir).name
        if src.exists():
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
            print(f"Copied: {asset_dir}")
    
    print(f"\nDocumentation built successfully!")
    print(f"Output: {build_dir}")

if __name__ == "__main__":
    build_docs()

