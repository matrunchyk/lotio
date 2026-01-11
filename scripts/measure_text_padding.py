#!/usr/bin/env python3
"""
Measure text padding in a rendered frame by finding leftmost and rightmost non-transparent pixels.
"""

import sys
from PIL import Image

def measure_padding(image_path):
    """Measure padding by finding leftmost and rightmost non-transparent pixels."""
    try:
        img = Image.open(image_path)
        width, height = img.size
        print(f"Image dimensions: {width}x{height}")
        
        # Convert to RGBA if not already
        if img.mode != 'RGBA':
            img = img.convert('RGBA')
        
        pixels = img.load()
        
        # Find leftmost, rightmost, topmost, bottommost non-transparent pixels
        leftmost = width
        rightmost = 0
        topmost = height
        bottommost = 0
        found_pixels = False
        
        for y in range(height):
            for x in range(width):
                r, g, b, a = pixels[x, y]
                if a > 0:  # Non-transparent pixel
                    found_pixels = True
                    leftmost = min(leftmost, x)
                    rightmost = max(rightmost, x)
                    topmost = min(topmost, y)
                    bottommost = max(bottommost, y)
        
        if not found_pixels:
            print("No non-transparent pixels found!")
            return
        
        # Calculate padding
        left_padding = leftmost
        right_padding = width - 1 - rightmost
        top_padding = topmost
        bottom_padding = height - 1 - bottommost
        
        # Calculate text width
        text_width = rightmost - leftmost + 1
        
        print(f"\nText bounds:")
        print(f"  Leftmost pixel: {leftmost}")
        print(f"  Rightmost pixel: {rightmost}")
        print(f"  Topmost pixel: {topmost}")
        print(f"  Bottommost pixel: {bottommost}")
        print(f"\nText dimensions:")
        print(f"  Text width: {text_width}px")
        print(f"  Text height: {bottommost - topmost + 1}px")
        print(f"\nPadding:")
        print(f"  Left padding: {left_padding}px ({left_padding/width*100:.2f}%)")
        print(f"  Right padding: {right_padding}px ({right_padding/width*100:.2f}%)")
        print(f"  Top padding: {top_padding}px ({top_padding/height*100:.2f}%)")
        print(f"  Bottom padding: {bottom_padding}px ({bottom_padding/height*100:.2f}%)")
        print(f"\nFrame dimensions: {width}x{height}")
        print(f"Text occupies: {text_width}px / {width}px = {text_width/width*100:.2f}% of frame width")
        
        # Validation: Check if padding matches expected values
        expected_padding = None
        if len(sys.argv) >= 3:
            try:
                expected_padding = int(sys.argv[2])
            except ValueError:
                pass
        
        # Check if text is edge-to-edge horizontally (1-5px padding is valid for edge-to-edge)
        if left_padding == 0 or right_padding == 0:
            print(f"\n✗ Invalid edge-to-edge: 0px padding detected (left: {left_padding}px, right: {right_padding}px)")
            print("  Edge-to-edge requires 1-5px transparency padding on each side")
        elif left_padding >= 1 and left_padding <= 5 and right_padding >= 1 and right_padding <= 5:
            print(f"\n✓ Text is edge-to-edge horizontally (valid padding: {left_padding}px left, {right_padding}px right)")
        elif left_padding < 10 and right_padding < 10:
            print(f"\n⚠ Text has minimal padding: {left_padding}px left, {right_padding}px right")
        else:
            print(f"\n✗ Text has significant padding: {left_padding}px left, {right_padding}px right")
        
        # Validate against expected padding if provided
        if expected_padding is not None:
            print(f"\nValidation (expected {expected_padding}px padding on all sides):")
            # For edge-to-edge (expected_padding >= 1), accept 1-5px range
            if expected_padding >= 1:
                left_ok = (left_padding >= 1 and left_padding <= 5)
                right_ok = (right_padding >= 1 and right_padding <= 5)
                top_ok = (top_padding >= 1 and top_padding <= 5)
                bottom_ok = (bottom_padding >= 1 and bottom_padding <= 5)
                
                if left_ok and right_ok and top_ok and bottom_ok:
                    print(f"  ✓ All padding is valid for edge-to-edge: {left_padding}px left, {right_padding}px right, {top_padding}px top, {bottom_padding}px bottom (1-5px range)")
                    return 0
                else:
                    print(f"  ✗ Padding mismatch (edge-to-edge requires 1-5px on all sides):")
                    if not left_ok:
                        print(f"    - Left: got {left_padding}px (expected 1-5px)")
                    if not right_ok:
                        print(f"    - Right: got {right_padding}px (expected 1-5px)")
                    if not top_ok:
                        print(f"    - Top: got {top_padding}px (expected 1-5px)")
                    if not bottom_ok:
                        print(f"    - Bottom: got {bottom_padding}px (expected 1-5px)")
                    return 1
            else:
                # For specific padding values, require exact match
                left_ok = (left_padding == expected_padding)
                right_ok = (right_padding == expected_padding)
                top_ok = (top_padding == expected_padding)
                bottom_ok = (bottom_padding == expected_padding)
                
                if left_ok and right_ok and top_ok and bottom_ok:
                    print(f"  ✓ All padding matches expected: {expected_padding}px on all sides")
                    return 0
                else:
                    print(f"  ✗ Padding mismatch:")
                    if not left_ok:
                        print(f"    - Left: got {left_padding}px, expected {expected_padding}px")
                    if not right_ok:
                        print(f"    - Right: got {right_padding}px, expected {expected_padding}px")
                    if not top_ok:
                        print(f"    - Top: got {top_padding}px, expected {expected_padding}px")
                    if not bottom_ok:
                        print(f"    - Bottom: got {bottom_padding}px, expected {expected_padding}px")
                    return 1
        
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 measure_text_padding.py <image_path> [expected_padding]")
        print("  image_path: Path to PNG image with transparent background")
        print("  expected_padding: (optional) Expected padding in pixels on all sides for validation")
        sys.exit(1)
    
    exit_code = measure_padding(sys.argv[1])
    sys.exit(exit_code if exit_code is not None else 0)
