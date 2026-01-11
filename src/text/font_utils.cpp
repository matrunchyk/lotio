#include "font_utils.h"
#include "../utils/string_utils.h"
#include "../utils/logging.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkSurface.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkColor.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

SkFontStyle getSkFontStyle(const std::string& styleStr) {
    if (styleStr.find("Bold") != std::string::npos && styleStr.find("Italic") != std::string::npos) {
        return SkFontStyle::BoldItalic();
    } else if (styleStr.find("Bold") != std::string::npos) {
        return SkFontStyle::Bold();
    } else if (styleStr.find("Italic") != std::string::npos) {
        return SkFontStyle::Italic();
    }
    return SkFontStyle::Normal();
}

// Helper function to measure rendered text width by scanning pixels (PIXEL_PERFECT mode)
// This measures the full text advance width including spacing, kerning, and glyph widths
static SkScalar measureRenderedTextWidth(sk_sp<SkTextBlob> blob, const SkFont& font, const SkRect& blobBounds) {
    // Create an off-screen surface to render the text
    // Use a surface that's wide enough to capture the full advance width
    int padding = 20;  // Extra padding for anti-aliasing
    int surfaceWidth = static_cast<int>(std::ceil(blobBounds.width() + std::abs(blobBounds.left()) + padding * 2));
    int surfaceHeight = static_cast<int>(std::ceil(blobBounds.height())) + padding * 2;
    
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        if (g_debug_mode) {
            LOG_DEBUG("[PIXEL_PERFECT] Fallback: invalid surface dimensions, using blobBounds.width(): " << blobBounds.width());
        }
        return blobBounds.width();
    }
    
    SkImageInfo info = SkImageInfo::MakeN32(
        surfaceWidth,
        surfaceHeight,
        kPremul_SkAlphaType
    );
    
    auto surface = SkSurfaces::Raster(info);
    if (!surface) {
        // Fallback to blob bounds if surface creation fails
        if (g_debug_mode) {
            LOG_DEBUG("[PIXEL_PERFECT] Fallback: surface creation failed, using blobBounds.width(): " << blobBounds.width());
        }
        return blobBounds.width();
    }
    
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    
    // Render the text starting at a known position (accounting for left side bearing)
    // This ensures we measure from the text start position, not just the leftmost pixel
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorBLACK);
    
    // Render at x=padding, y=padding (known start position)
    // The blob's left() might be negative (left side bearing), so we adjust
    float xStart = padding - blobBounds.left();  // Start position accounting for left bearing
    float yStart = padding - blobBounds.top();
    canvas->drawTextBlob(blob, xStart, yStart, paint);
    
    // Get the image and find actual rendered bounds
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        if (g_debug_mode) {
            LOG_DEBUG("[PIXEL_PERFECT] Fallback: image snapshot failed, using blobBounds.width(): " << blobBounds.width());
        }
        return blobBounds.width();
    }
    
    // Read pixels and find actual rendered bounds
    SkPixmap pixmap;
    if (!image->peekPixels(&pixmap)) {
        if (g_debug_mode) {
            LOG_DEBUG("[PIXEL_PERFECT] Fallback: peekPixels failed, using blobBounds.width(): " << blobBounds.width());
        }
        return blobBounds.width();
    }
    
    // Find rightmost non-transparent pixel (from the start position)
    // This gives us the full advance width including all spacing and kerning
    int rightmostPixel = 0;
    bool foundPixel = false;
    
    // Scan from the start position to find the rightmost pixel
    // This accounts for the full text width including spacing
    int startX = static_cast<int>(xStart);
    for (int y = 0; y < pixmap.height(); y++) {
        const uint32_t* row = pixmap.addr32(0, y);
        for (int x = startX; x < pixmap.width(); x++) {
            SkColor color = row[x];
            if (SkColorGetA(color) > 0) {  // Non-transparent pixel
                rightmostPixel = std::max(rightmostPixel, x);
                foundPixel = true;
            }
        }
    }
    
    if (foundPixel && rightmostPixel >= startX) {
        // Measure from start position to rightmost pixel
        // This gives us the full advance width including spacing, kerning, and glyph widths
        SkScalar renderedWidth = static_cast<SkScalar>(rightmostPixel - startX + 1) + 1.0f;  // +1px safety margin
        
        // Use the maximum of rendered width and blob bounds width to ensure we account for all spacing
        // The blob bounds include full glyph bounding boxes which account for kerning and spacing
        SkScalar finalWidth = std::max(renderedWidth, blobBounds.width());
        
        if (g_debug_mode) {
            LOG_DEBUG("[PIXEL_PERFECT] Measured width: " << finalWidth << " (rendered: " << renderedWidth 
                      << ", blobBounds: " << blobBounds.width() << ", startX: " << startX << ", rightmost: " << rightmostPixel << ")");
        }
        return finalWidth;
    }
    
    // Fallback to blob bounds (which includes full spacing and kerning)
    if (g_debug_mode) {
        LOG_DEBUG("[PIXEL_PERFECT] Fallback: no pixels found, using blobBounds.width(): " << blobBounds.width());
    }
    return blobBounds.width();
}

SkScalar measureTextWidth(
    SkFontMgr* fontMgr,
    const std::string& fontFamily,
    const std::string& fontStyle,
    const std::string& fontName,  // Full name like "SegoeUI-Bold"
    float fontSize,
    const std::string& text,
    TextMeasurementMode mode
) {
    // Try to get typeface using family and style
    sk_sp<SkTypeface> typeface = fontMgr->matchFamilyStyle(
        fontFamily.c_str(),
        getSkFontStyle(fontStyle)
    );
    
    // Fallback: try with full font name
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle(fontName.c_str(), SkFontStyle::Normal());
    }
    
    // Last resort: try legacy method
    if (!typeface) {
        typeface = fontMgr->legacyMakeTypeface(fontName.c_str(), SkFontStyle::Normal());
    }
    
    if (!typeface) {
        LOG_DEBUG("Warning: Could not find typeface for " << fontName << ", using default");
        typeface = fontMgr->legacyMakeTypeface(nullptr, SkFontStyle::Normal());
    }
    
    SkFont font(typeface, fontSize);
    
    // Enable proper text rendering settings for ACCURATE and PIXEL_PERFECT modes
    if (mode != TextMeasurementMode::FAST) {
        font.setEdging(SkFont::Edging::kAntiAlias);
        font.setSubpixel(true);
        font.setHinting(SkFontHinting::kNormal);
    }
    
    // Split text by newlines (\r or \n) and measure each line
    // Return the width of the longest line
    SkScalar maxWidth = 0.0f;
    std::string currentLine;
    
    for (size_t i = 0; i <= text.length(); i++) {
        if (i == text.length() || text[i] == '\r' || text[i] == '\n') {
            // Measure current line
            if (!currentLine.empty()) {
                SkScalar width = 0.0f;
                
                if (mode == TextMeasurementMode::FAST) {
                    // FAST mode: Use measureText() with bounds (current implementation)
                    SkRect bounds;
                    font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
                    width = bounds.width();
                } else {
                    // ACCURATE or PIXEL_PERFECT mode: Use SkTextBlob
                    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(
                        currentLine.c_str(),
                        font
                    );
                    
                    if (blob) {
                        SkRect blobBounds = blob->bounds();
                        
                        if (mode == TextMeasurementMode::ACCURATE) {
                            // ACCURATE mode: Use blob bounds
                            width = blobBounds.width();
                        } else {
                            // PIXEL_PERFECT mode: Render and measure actual pixels
                            width = measureRenderedTextWidth(blob, font, blobBounds);
                        }
                    } else {
                        // Fallback if blob creation fails
                        SkRect bounds;
                        font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
                        width = bounds.width();
                    }
                }
                
                maxWidth = std::max(maxWidth, width);
                if (g_debug_mode && (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)) {
                    LOG_COUT("[DEBUG] Measured line: \"" << currentLine << "\" width: " << width << " (mode: " << (mode == TextMeasurementMode::FAST ? "FAST" : (mode == TextMeasurementMode::ACCURATE ? "ACCURATE" : "PIXEL_PERFECT")) << ")") << std::endl;
                }
            }
            currentLine.clear();
            
            // Skip \r\n combination
            if (i < text.length() && text[i] == '\r' && i + 1 < text.length() && text[i + 1] == '\n') {
                i++;  // Skip the \n
            }
        } else {
            currentLine += text[i];
        }
    }
    
    // Handle last line if text doesn't end with newline
    if (!currentLine.empty()) {
        SkScalar width = 0.0f;
        
        if (mode == TextMeasurementMode::FAST) {
            SkRect bounds;
            font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
            width = bounds.width();
        } else {
            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(
                currentLine.c_str(),
                font
            );
            
            if (blob) {
                SkRect blobBounds = blob->bounds();
                
                if (mode == TextMeasurementMode::ACCURATE) {
                    width = blobBounds.width();
                } else {
                    width = measureRenderedTextWidth(blob, font, blobBounds);
                }
            } else {
                SkRect bounds;
                font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
                width = bounds.width();
            }
        }
        
        maxWidth = std::max(maxWidth, width);
        if (g_debug_mode && (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)) {
            LOG_COUT("[DEBUG] Measured line: \"" << currentLine << "\" width: " << width << " (mode: " << (mode == TextMeasurementMode::FAST ? "FAST" : (mode == TextMeasurementMode::ACCURATE ? "ACCURATE" : "PIXEL_PERFECT")) << ")") << std::endl;
        }
    }
    
    if (g_debug_mode && (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)) {
        LOG_COUT("[DEBUG] Multiline text - longest line width: " << maxWidth) << std::endl;
    }
    
    return maxWidth;
}

FontInfo extractFontInfoFromJson(const std::string& json, const std::string& layerName) {
    FontInfo info;
    info.size = 0.0f;
    info.textBoxWidth = 0.0f;  // Initialize to 0 to avoid garbage values
    
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        
        // Find the layer by name
        if (!j.contains("layers") || !j["layers"].is_array()) {
            LOG_DEBUG("No layers array found in JSON");
            return info;
        }
        
        nlohmann::json* foundLayer = nullptr;
        for (auto& layer : j["layers"]) {
            if (layer.contains("nm") && layer["nm"].is_string() && layer["nm"].get<std::string>() == layerName) {
                // Check if it's a text layer (ty:5)
                if (layer.contains("ty") && layer["ty"].is_number() && layer["ty"].get<int>() == 5) {
                    foundLayer = &layer;
                    break;
                }
            }
        }
        
        if (foundLayer == nullptr) {
            LOG_DEBUG("Layer " << layerName << " not found or not a text layer (ty:5)");
            return info;
        }
        
        // Extract text style from layers[i]["t"]["d"]["k"][0]["s"]
        if (foundLayer->contains("t") && (*foundLayer)["t"].is_object()) {
            auto& t = (*foundLayer)["t"];
            if (t.contains("d") && t["d"].is_object()) {
                auto& d = t["d"];
                if (d.contains("k") && d["k"].is_array() && d["k"].size() > 0) {
                    auto& k = d["k"];
                    if (k[0].is_object() && k[0].contains("s") && k[0]["s"].is_object()) {
                        auto& s = k[0]["s"];
                        
                        // Extract font size
                        if (s.contains("s") && s["s"].is_number()) {
                            info.size = s["s"].get<float>();
                        }
                        
                        // Extract font name
                        if (s.contains("f") && s["f"].is_string()) {
                            info.name = s["f"].get<std::string>();
                        }
                        
                        // Extract text content
                        if (s.contains("t") && s["t"].is_string()) {
                            info.text = s["t"].get<std::string>();
                            // Handle escaped newlines in extracted text (\r, \n, \u0003)
                            // Convert escaped sequences to actual characters (unescape)
                            // Normalize all to \r for Lottie compatibility
                            replaceAllInPlace(info.text, "\\r", "\r");
                            replaceAllInPlace(info.text, "\\n", "\r");  // Convert \n to \r for Lottie
                            replaceAllInPlace(info.text, "\\u0003", "\r");
                            replaceCharInPlace(info.text, '\x03', '\r');
                            // Also convert any existing \n to \r for consistency
                            replaceCharInPlace(info.text, '\n', '\r');
                        }
                        
                        // Extract text box size (sz)
                        if (s.contains("sz") && s["sz"].is_array() && s["sz"].size() >= 2) {
                            if (s["sz"][0].is_number()) {
                                info.textBoxWidth = s["sz"][0].get<float>();
                            }
                        }
                    }
                }
            }
        }
        
        // Extract font family and style from fonts.list
        if (j.contains("fonts") && j["fonts"].is_object() && j["fonts"].contains("list") && j["fonts"]["list"].is_array()) {
            for (auto& fontDef : j["fonts"]["list"]) {
                if (fontDef.is_object() && fontDef.contains("fName") && fontDef["fName"].is_string()) {
                    if (fontDef["fName"].get<std::string>() == info.name) {
                        if (fontDef.contains("fFamily") && fontDef["fFamily"].is_string()) {
                            info.family = fontDef["fFamily"].get<std::string>();
                        }
                        if (fontDef.contains("fStyle") && fontDef["fStyle"].is_string()) {
                            info.style = fontDef["fStyle"].get<std::string>();
                        }
                        break;
                    }
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_DEBUG("Failed to parse JSON in extractFontInfoFromJson: " << e.what());
    }
    
    return info;
}

