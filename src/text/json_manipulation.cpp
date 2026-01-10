#include "json_manipulation.h"
#include "../utils/string_utils.h"
#include "../utils/logging.h"
#include <nlohmann/json.hpp>
#include <cmath>

void adjustTextAnimatorPosition(
    std::string& json,
    const std::string& layerName,
    float widthDiff
) {
    if (std::abs(widthDiff) < 0.1f) {
        return;  // No significant change
    }
    
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        
        // Find the layer by name
        if (!j.contains("layers") || !j["layers"].is_array()) {
            return;
        }
        
        nlohmann::json* foundLayer = nullptr;
        for (auto& layer : j["layers"]) {
            if (layer.contains("nm") && layer["nm"].is_string() && layer["nm"].get<std::string>() == layerName) {
                foundLayer = &layer;
                break;
            }
        }
        
        if (foundLayer == nullptr) {
            return;  // Layer not found
        }
        
        // Find the position property in text animator: layers[i]["t"]["a"][0]["a"]["p"]
        if (foundLayer->contains("t") && (*foundLayer)["t"].is_object()) {
            auto& t = (*foundLayer)["t"];
            if (t.contains("a") && t["a"].is_array() && t["a"].size() > 0) {
                // Iterate through animators
                for (auto& animator : t["a"]) {
                    if (animator.is_object() && animator.contains("a") && animator["a"].is_object()) {
                        auto& a = animator["a"];
                        // Look for position property "p" with animated keyframes
                        if (a.contains("p") && a["p"].is_object()) {
                            auto& p = a["p"];
                            // Check if animated (a: 1)
                            if (p.contains("a") && p["a"].is_number() && p["a"].get<int>() == 1) {
                                // Check if it has keyframes
                                if (p.contains("k") && p["k"].is_array()) {
                                    // Adjust X values in keyframes
                                    for (auto& keyframe : p["k"]) {
                                        if (keyframe.is_object() && keyframe.contains("s") && keyframe["s"].is_array() && keyframe["s"].size() >= 1) {
                                            float x = keyframe["s"][0].get<float>();
                                            float newX = x;
                                            if (widthDiff > 0.1f) {
                                                // Text got wider - move further left (more negative)
                                                newX = x - widthDiff;
                                            } else if (widthDiff < -0.1f) {
                                                // Text got narrower - keep same position
                                                newX = x;
                                            }
                                            keyframe["s"][0] = newX;
                                            
                                            if (g_debug_mode) {
                                                LOG_COUT("[DEBUG] Adjusted X value: " << x << " -> " << newX) << std::endl;
                                            }
                                        }
                                    }
                                    
                                    // Serialize back to JSON
                                    json = j.dump(4);
                                    return;  // Found and adjusted, done
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Error parsing JSON in adjustTextAnimatorPosition: " << e.what()) << std::endl;
        }
    }
}

void modifyTextLayerInJson(
    std::string& json,
    const std::string& layerName,
    const std::string& newText,
    float newSize
) {
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        
        // Find the layer by name
        if (!j.contains("layers") || !j["layers"].is_array()) {
            if (g_debug_mode) {
                LOG_COUT("[DEBUG] Warning: No layers array found in JSON") << std::endl;
            }
            return;
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
            if (g_debug_mode) {
                LOG_COUT("[DEBUG] Warning: Could not find text layer: " << layerName) << std::endl;
            }
            return;
        }
        
        // Modify text and size at layers[i]["t"]["d"]["k"][0]["s"]
        if (foundLayer->contains("t") && (*foundLayer)["t"].is_object()) {
            auto& t = (*foundLayer)["t"];
            if (t.contains("d") && t["d"].is_object()) {
                auto& d = t["d"];
                if (d.contains("k") && d["k"].is_array() && d["k"].size() > 0) {
                    auto& k = d["k"];
                    if (k[0].is_object() && k[0].contains("s") && k[0]["s"].is_object()) {
                        auto& s = k[0]["s"];
                        
                        // Update text content
                        s["t"] = newText;
                        
                        // Update font size
                        s["s"] = newSize;
                        
                        // Serialize back to JSON with 4-space indentation
                        json = j.dump(4);
                        
                        if (g_debug_mode) {
                            LOG_COUT("[DEBUG] Text replacement successful for " << layerName << ": \"" << newText << "\"") << std::endl;
                        }
                        return;
                    }
                }
            }
        }
        
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find text style object for layer: " << layerName) << std::endl;
        }
    } catch (const nlohmann::json::exception& e) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Error parsing JSON in modifyTextLayerInJson: " << e.what()) << std::endl;
        }
    }
}

void normalizeLottieTextNewlines(std::string& json) {
    // Some Lottie producers use U+0003 (ETX) as a soft line-break marker inside "t".
    // Skottie interprets '\r' as a newline in Lottie JSON.
    // We normalize it to '\r' (which will be escaped as "\\r" in JSON strings).
    //
    // We handle both representations:
    // 1) Escaped form inside JSON: "\\u0003"
    // 2) Literal byte form already present in the string: '\x03'
    
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        bool modified = false;
        
        // Recursively find and normalize all "t" string fields in text layers
        if (j.contains("layers") && j["layers"].is_array()) {
            for (auto& layer : j["layers"]) {
                // Check if it's a text layer (ty:5)
                if (layer.contains("ty") && layer["ty"].is_number() && layer["ty"].get<int>() == 5) {
                    // Navigate to layers[i]["t"]["d"]["k"][0]["s"]["t"]
                    if (layer.contains("t") && layer["t"].is_object()) {
                        auto& t = layer["t"];
                        if (t.contains("d") && t["d"].is_object()) {
                            auto& d = t["d"];
                            if (d.contains("k") && d["k"].is_array() && d["k"].size() > 0) {
                                auto& k = d["k"];
                                if (k[0].is_object() && k[0].contains("s") && k[0]["s"].is_object()) {
                                    auto& s = k[0]["s"];
                                    if (s.contains("t") && s["t"].is_string()) {
                                        std::string text = s["t"].get<std::string>();
                                        std::string originalText = text;
                                        
                                        // Replace \u0003 with \r
                                        replaceAllInPlace(text, "\\u0003", "\\r");
                                        replaceCharInPlace(text, '\x03', '\r');
                                        
                                        if (text != originalText) {
                                            s["t"] = text;
                                            modified = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        if (modified) {
            json = j.dump(4);
        }
        
        // Also do global replacement for any remaining escaped sequences
        const auto replacedEscaped = replaceAllInPlace(json, "\\u0003", "\\r");
        const auto replacedLiteral = replaceCharInPlace(json, '\x03', '\r');
        
        LOG_DEBUG("Text newline normalization: replacedEscaped=\\u0003->\\r x" << replacedEscaped
                  << ", replacedLiteral=0x03->\\r x" << replacedLiteral);
    } catch (const nlohmann::json::exception& e) {
        // Fallback to simple string replacement if JSON parsing fails
        const auto replacedEscaped = replaceAllInPlace(json, "\\u0003", "\\r");
        const auto replacedLiteral = replaceCharInPlace(json, '\x03', '\r');
        
        LOG_DEBUG("Text newline normalization (fallback): replacedEscaped=\\u0003->\\r x" << replacedEscaped
                  << ", replacedLiteral=0x03->\\r x" << replacedLiteral);
    }
}
