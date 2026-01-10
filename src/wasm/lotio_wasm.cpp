#include <emscripten.h>
#include <emscripten/bind.h>
#include "../core/animation_setup.h"
#include "../text/json_manipulation.h"
#include "../text/layer_overrides.h"
#include "../text/text_processor.h"
#include "../text/font_utils.h"
#include "../text/text_sizing.h"
#include "../utils/string_utils.h"
#include "../utils/logging.h"
#include "modules/skottie/include/Skottie.h"
#include "modules/skresources/include/SkResources.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkImage.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkStream.h"
#include "include/ports/SkFontMgr_data.h"
#include "include/encode/SkPngEncoder.h"
#include <nlohmann/json.hpp>
#include <map>
#include <vector>
#include "include/codec/SkCodec.h"
#include "include/codec/SkPngDecoder.h"
#include <string>
#include <memory>
#include <sstream>
#include <regex>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>

// Helper to parse layer overrides from string (for WASM)
static std::map<std::string, LayerOverride> parseLayerOverridesFromString(const std::string& json) {
    std::map<std::string, LayerOverride> configs;
    
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        
        // Parse textLayers section
        if (j.contains("textLayers") && j["textLayers"].is_object()) {
            for (auto& [layerName, layerConfig] : j["textLayers"].items()) {
                LayerOverride config;
                
                // Extract optional fields
                if (layerConfig.contains("minSize") && layerConfig["minSize"].is_number()) {
                    config.minSize = layerConfig["minSize"].get<float>();
                } else {
                    config.minSize = 0.0f;
                }
                
                if (layerConfig.contains("maxSize") && layerConfig["maxSize"].is_number()) {
                    config.maxSize = layerConfig["maxSize"].get<float>();
                } else {
                    config.maxSize = 0.0f;
                }
                
                if (layerConfig.contains("fallbackText") && layerConfig["fallbackText"].is_string()) {
                    config.fallbackText = layerConfig["fallbackText"].get<std::string>();
                } else {
                    config.fallbackText = "";
                }
                
                if (layerConfig.contains("textBoxWidth") && layerConfig["textBoxWidth"].is_number()) {
                    config.textBoxWidth = layerConfig["textBoxWidth"].get<float>();
                } else {
                    config.textBoxWidth = 0.0f;
                }
                
                if (layerConfig.contains("value") && layerConfig["value"].is_string()) {
                    config.value = layerConfig["value"].get<std::string>();
                } else {
                    config.value = "";
                }
                
                // Handle \u0003 (ETX) - convert to \r for Lottie newlines
                replaceAllInPlace(config.value, "\\u0003", "\r");
                replaceCharInPlace(config.value, '\x03', '\r');
                
                configs[layerName] = config;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        EM_ASM({
            console.error('[ERROR] Failed to parse layer-overrides.json:', UTF8ToString($0));
        }, e.what());
    }
    
    return configs;
}

// Helper to parse image layers from string (for WASM)
static std::map<std::string, ImageLayerOverride> parseImageLayersFromString(const std::string& json) {
    std::map<std::string, ImageLayerOverride> imageLayers;
    
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        
        // Parse imageLayers section
        if (j.contains("imageLayers") && j["imageLayers"].is_object()) {
            for (auto& [assetId, layerConfig] : j["imageLayers"].items()) {
                if (assetId.empty()) {
                    continue;
                }
                
                ImageLayerOverride config;
                
                // Extract optional fields
                if (layerConfig.contains("filePath") && layerConfig["filePath"].is_string()) {
                    config.filePath = layerConfig["filePath"].get<std::string>();
                } else {
                    config.filePath = "";
                }
                
                if (layerConfig.contains("fileName") && layerConfig["fileName"].is_string()) {
                    config.fileName = layerConfig["fileName"].get<std::string>();
                } else {
                    config.fileName = "";
                }
                
                imageLayers[assetId] = config;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        EM_ASM({
            console.error('[ERROR] Failed to parse image layers from layer-overrides.json:', UTF8ToString($0));
        }, e.what());
    }
    
    return imageLayers;
}

// Helper to process layer overrides from string (for WASM)
static void processLayerOverridesFromString(std::string& json_data, const std::string& layer_overrides_json, SkFontMgr* fontMgr = nullptr, float textPadding = 0.97f, TextMeasurementMode textMeasurementMode = TextMeasurementMode::ACCURATE) {
    if (layer_overrides_json.empty()) {
        return;
    }
    
    auto layerOverrides = parseLayerOverridesFromString(layer_overrides_json);
    auto imageLayers = parseImageLayersFromString(layer_overrides_json);
    
    // Process image layer overrides first
    if (!imageLayers.empty()) {
        try {
            nlohmann::json j = nlohmann::json::parse(json_data);
            
            if (j.contains("assets") && j["assets"].is_array()) {
                // Process each asset
                for (const auto& [assetId, imageConfig] : imageLayers) {
                    EM_ASM({
                        console.log('[DEBUG] Processing image override for asset ID:', UTF8ToString($0));
                    }, assetId.c_str());
                    
                    // Find asset by ID
                    nlohmann::json* foundAsset = nullptr;
                    for (auto& asset : j["assets"]) {
                        if (asset.contains("id") && asset["id"].is_string() && asset["id"].get<std::string>() == assetId) {
                            foundAsset = &asset;
                            break;
                        }
                    }
                    
                    if (foundAsset == nullptr) {
                        EM_ASM({
                            console.warn('[WARNING] Asset ID not found in assets array:', UTF8ToString($0));
                        }, assetId.c_str());
                        continue;
                    }
                    
                    // Determine the full path from filePath and fileName
                    std::string dir;
                    std::string filename;
                    
                    if (imageConfig.filePath.empty() && !imageConfig.fileName.empty()) {
                        // filePath is empty string, fileName contains full path
                        size_t lastSlash = imageConfig.fileName.find_last_of("/\\");
                        dir = (lastSlash != std::string::npos) ? imageConfig.fileName.substr(0, lastSlash + 1) : "";
                        filename = (lastSlash != std::string::npos) ? imageConfig.fileName.substr(lastSlash + 1) : imageConfig.fileName;
                    } else if (!imageConfig.filePath.empty() && !imageConfig.fileName.empty()) {
                        // Both specified
                        dir = imageConfig.filePath;
                        if (dir.back() != '/' && dir.back() != '\\') {
                            dir += "/";
                        }
                        filename = imageConfig.fileName;
                    } else if (!imageConfig.filePath.empty() && imageConfig.fileName.empty()) {
                        // Only filePath specified - use default fileName from assets[].p
                        dir = imageConfig.filePath;
                        if (dir.back() != '/' && dir.back() != '\\') {
                            dir += "/";
                        }
                        // Extract filename from assets[].p
                        if ((*foundAsset).contains("p") && (*foundAsset)["p"].is_string()) {
                            filename = (*foundAsset)["p"].get<std::string>();
                        } else {
                            EM_ASM({
                                console.warn('[WARNING] Could not find "p" property for asset ID:', UTF8ToString($0), ', skipping');
                            }, assetId.c_str());
                            continue;
                        }
                    } else {
                        // Both empty - skip
                        EM_ASM({
                            console.warn('[WARNING] Both filePath and fileName are empty for asset ID:', UTF8ToString($0));
                        }, assetId.c_str());
                        continue;
                    }
                    
                    if (dir == "/" || dir == "\\") {
                        dir = "";
                    }
                    
                    EM_ASM({
                        console.log('[DEBUG] Split image path - directory:', UTF8ToString($0), ', filename:', UTF8ToString($1));
                    }, dir.c_str(), filename.c_str());
                    
                    // Update u and p properties
                    (*foundAsset)["u"] = dir;
                    (*foundAsset)["p"] = filename;
                    
                    EM_ASM({
                        console.log('[DEBUG] Updated asset:', UTF8ToString($0), '- u:', UTF8ToString($1), ', p:', UTF8ToString($2));
                        console.log('[DEBUG] Image override applied successfully for asset ID:', UTF8ToString($0));
                    }, assetId.c_str(), dir.c_str(), filename.c_str());
                }
                
                // Serialize back to JSON
                json_data = j.dump(4);
            } else {
                EM_ASM({
                    console.warn('[WARNING] Assets array not found in JSON - image overrides will not be applied');
                });
            }
        } catch (const nlohmann::json::exception& e) {
            EM_ASM({
                console.error('[ERROR] Failed to parse JSON for image asset processing:', UTF8ToString($0));
            }, e.what());
        }
    }
    
    if (layerOverrides.empty()) {
        return;
    }
    
    // Extract animation width from JSON
    float animationWidth = 720.0f;
    try {
        nlohmann::json j = nlohmann::json::parse(json_data);
        if (j.contains("w") && j["w"].is_number()) {
            animationWidth = j["w"].get<float>();
        }
    } catch (const nlohmann::json::exception&) {
        // Use default width if parsing fails
    }
    
    // Use the provided font manager (with registered fonts) for text measurement
    // If not provided, fall back to empty (but this won't work for measurement)
    SkFontMgr* tempFontMgr = fontMgr;
    if (!tempFontMgr) {
        // Fallback - but this won't work for measurement
        static sk_sp<SkFontMgr> emptyMgr = SkFontMgr::RefEmpty();
        tempFontMgr = emptyMgr.get();
    }
    
    // First pass: extract all font info and calculate optimal sizes
    struct LayerModification {
        std::string layerName;
        std::string textToUse;
        float optimalSize;
        float originalTextWidth;
        float newTextWidth;
    };
    std::vector<LayerModification> modifications;
    
    for (const auto& [layerName, config] : layerOverrides) {
        FontInfo fontInfo = extractFontInfoFromJson(json_data, layerName);
        
        if (fontInfo.name.empty()) {
            continue;
        }
        
        std::string textToUse = config.value.empty() ? fontInfo.text : config.value;
        
        if (textToUse.empty()) {
            continue;
        }
        
        float targetWidth = animationWidth;
        if (config.textBoxWidth > 0) {
            targetWidth = config.textBoxWidth;
        } else if (fontInfo.textBoxWidth > 0) {
            targetWidth = fontInfo.textBoxWidth;
        }
        
        float currentWidth = measureTextWidth(tempFontMgr, fontInfo.family, fontInfo.style,
                                             fontInfo.name, fontInfo.size, textToUse, textMeasurementMode);
        
        // If minSize and maxSize are not specified, no auto-fit - just use original size or update text value
        float optimalSize = fontInfo.size;
        float finalWidth = currentWidth;
        
        if (config.minSize > 0 && config.maxSize > 0) {
            // Apply padding to target width to prevent text from touching edges
            // textPadding: 0.97 means 97% of target width (3% padding, 1.5% per side)
            float paddedTargetWidth = targetWidth * textPadding;
            
            optimalSize = calculateOptimalFontSize(
                tempFontMgr,
                fontInfo,
                config,
                textToUse,
                paddedTargetWidth,
                textMeasurementMode
            );
            
            if (optimalSize >= 0) {
                finalWidth = measureTextWidth(tempFontMgr, fontInfo.family, fontInfo.style,
                                             fontInfo.name, optimalSize, textToUse, textMeasurementMode);
            }
            
            if (optimalSize < 0) {
                textToUse = config.fallbackText;
                FontInfo fallbackFontInfo = fontInfo;
                fallbackFontInfo.size = config.minSize;
                
                float fallbackMinWidth = measureTextWidth(tempFontMgr, fallbackFontInfo.family, 
                                                         fallbackFontInfo.style, fallbackFontInfo.name, 
                                                         config.minSize, textToUse, textMeasurementMode);
                
                if (fallbackMinWidth > paddedTargetWidth) {
                    optimalSize = config.minSize;
                    finalWidth = measureTextWidth(tempFontMgr, fallbackFontInfo.family,
                                                 fallbackFontInfo.style, fallbackFontInfo.name,
                                                 config.minSize, textToUse, textMeasurementMode);
                } else {
                    float min = config.minSize;
                    float max = config.maxSize;
                    float bestSize = config.minSize;
                    
                    for (int i = 0; i < 10; i++) {
                        float testSize = (min + max) / 2.0f;
                        float testWidth = measureTextWidth(tempFontMgr, fallbackFontInfo.family,
                                                          fallbackFontInfo.style, fallbackFontInfo.name,
                                                          testSize, textToUse, textMeasurementMode);
                        
                        if (testWidth <= paddedTargetWidth) {
                            bestSize = testSize;
                            min = testSize;
                        } else {
                            max = testSize;
                        }
                    }
                    
                    optimalSize = std::min(bestSize, config.maxSize);
                    finalWidth = measureTextWidth(tempFontMgr, fallbackFontInfo.family,
                                                 fallbackFontInfo.style, fallbackFontInfo.name,
                                                 optimalSize, textToUse, textMeasurementMode);
                }
            }
        }
        
        float originalTextWidth = currentWidth;
        float newTextWidth = finalWidth;
        
        modifications.push_back({layerName, textToUse, optimalSize, originalTextWidth, newTextWidth});
    }
    
    // Second pass: apply modifications
    for (auto it = modifications.rbegin(); it != modifications.rend(); ++it) {
        modifyTextLayerInJson(json_data, it->layerName, it->textToUse, it->optimalSize);
        
        float widthDiff = it->newTextWidth - it->originalTextWidth;
        if (std::abs(widthDiff) > 0.1f) {
            float adjustment = std::abs(widthDiff);
            adjustTextAnimatorPosition(json_data, it->layerName, adjustment);
        }
    }
}

// Global font manager for creating typefaces from data
// We'll use SkFontMgr_New_Custom_Data which is designed for this use case
static sk_sp<SkFontMgr> g_dataFontMgr = nullptr;

// Initialize the data font manager (call once)
static void initDataFontMgr() {
    if (!g_dataFontMgr) {
        // Create an empty custom data font manager
        // We'll add fonts to it dynamically
        std::vector<sk_sp<SkData>> emptyData;
        g_dataFontMgr = SkFontMgr_New_Custom_Data(SkSpan<sk_sp<SkData>>(emptyData));
        if (!g_dataFontMgr) {
            // Fallback to empty
            g_dataFontMgr = SkFontMgr::RefEmpty();
        }
    }
}

// Helper function to create typeface from data
// Uses the custom data font manager which supports makeFromData
static sk_sp<SkTypeface> createTypefaceFromData(sk_sp<SkData> data) {
    if (!data) {
        return nullptr;
    }
    
    initDataFontMgr();
    
    // Use the data font manager to create typeface
    // SkFontMgr_New_Custom_Data should support makeFromData
    auto typeface = g_dataFontMgr->makeFromData(data);
    return typeface;
}

// Custom font manager that can look up fonts by name
class CustomFontMgr : public SkFontMgr {
public:
    void registerFont(const std::string& name, sk_sp<SkData> fontData) {
        // Store font data
        fontData_[name] = fontData;
        
        // Try to create typeface using the helper function
        // This will use FreeType if available
        auto typeface = createTypefaceFromData(fontData);
        if (typeface) {
            fonts_[name] = typeface;
            // Also store by family name
            SkString familyName;
            typeface->getFamilyName(&familyName);
            if (!familyName.isEmpty()) {
                familyFonts_[familyName.c_str()] = typeface;
            }
            
            EM_ASM({
                console.log('Font registered: name=' + UTF8ToString($0) + ', family=' + UTF8ToString($1));
            }, name.c_str(), familyName.c_str());
        } else {
            // Store data for lazy creation
            EM_ASM({
                console.warn('Could not create typeface immediately for: ' + UTF8ToString($0) + '. Will try on-demand.');
            }, name.c_str());
        }
    }
    
protected:
    int onCountFamilies() const override {
        return familyFonts_.size();
    }
    
    void onGetFamilyName(int index, SkString* familyName) const override {
        int i = 0;
        for (const auto& [name, tf] : familyFonts_) {
            if (i == index) {
                *familyName = name.c_str();
                return;
            }
            i++;
        }
    }
    
    sk_sp<SkFontStyleSet> onCreateStyleSet(int index) const override {
        int i = 0;
        for (const auto& [name, tf] : familyFonts_) {
            if (i == index) {
                return sk_sp<SkFontStyleSet>(new SingleFontStyleSet(tf));
            }
            i++;
        }
        return nullptr;
    }
    
    sk_sp<SkFontStyleSet> onMatchFamily(const char familyName[]) const override {
        // Try exact match first
        auto it = familyFonts_.find(familyName);
        if (it != familyFonts_.end()) {
            return sk_sp<SkFontStyleSet>(new SingleFontStyleSet(it->second));
        }
        
        // Try partial match (e.g., "Open Sans" matches "OpenSans-Bold")
        for (const auto& [name, tf] : fonts_) {
            if (name.find(familyName) != std::string::npos || 
                std::string(familyName).find(name) != std::string::npos) {
                return sk_sp<SkFontStyleSet>(new SingleFontStyleSet(tf));
            }
        }
        
        return nullptr;
    }
    
    sk_sp<SkTypeface> onMatchFamilyStyle(const char familyName[],
                                        const SkFontStyle& /* style */) const override {
        // First try exact match by family name
        auto familyIt = familyFonts_.find(familyName);
        if (familyIt != familyFonts_.end()) {
            return familyIt->second;
        }
        
        // Try exact match by font name (e.g., "OpenSans-Bold")
        auto fontIt = fonts_.find(familyName);
        if (fontIt != fonts_.end()) {
            return fontIt->second;
        }
        
        // Try matching by font name (e.g., "OpenSans-Bold" matches "Open Sans")
        for (const auto& [fontName, data] : fontData_) {
            // Check if fontName contains familyName or vice versa
            std::string fontNameLower = fontName;
            std::string familyNameLower = familyName;
            std::transform(fontNameLower.begin(), fontNameLower.end(), fontNameLower.begin(), ::tolower);
            std::transform(familyNameLower.begin(), familyNameLower.end(), familyNameLower.begin(), ::tolower);
            
            // Remove spaces and hyphens for comparison
            fontNameLower.erase(std::remove(fontNameLower.begin(), fontNameLower.end(), ' '), fontNameLower.end());
            fontNameLower.erase(std::remove(fontNameLower.begin(), fontNameLower.end(), '-'), fontNameLower.end());
            familyNameLower.erase(std::remove(familyNameLower.begin(), familyNameLower.end(), ' '), familyNameLower.end());
            familyNameLower.erase(std::remove(familyNameLower.begin(), familyNameLower.end(), '-'), familyNameLower.end());
            
            if (fontNameLower.find(familyNameLower) != std::string::npos || 
                familyNameLower.find(fontNameLower) != std::string::npos) {
                // Found a match - create typeface if not cached
                auto cachedIt = fonts_.find(fontName);
                if (cachedIt != fonts_.end()) {
                    return cachedIt->second;
                }
                
                // Create typeface from stored data
                auto typeface = createTypefaceFromData(data);
                if (typeface) {
                    // Cache it
                    const_cast<CustomFontMgr*>(this)->fonts_[fontName] = typeface;
                    // Also cache by family name
                    SkString tfFamilyName;
                    typeface->getFamilyName(&tfFamilyName);
                    if (!tfFamilyName.isEmpty()) {
                        const_cast<CustomFontMgr*>(this)->familyFonts_[tfFamilyName.c_str()] = typeface;
                    }
                    return typeface;
                }
            }
        }
        
        return nullptr;
    }
    
    sk_sp<SkTypeface> onMatchFamilyStyleCharacter(const char familyName[],
                                                  const SkFontStyle& style,
                                                  const char* /* bcp47 */[],
                                                  int /* bcp47Count */,
                                                  SkUnichar /* character */) const override {
        return onMatchFamilyStyle(familyName, style);
    }
    
    sk_sp<SkTypeface> onMakeFromData(sk_sp<SkData> data, int ttcIndex) const override {
        // Use empty font manager as fallback (it should still be able to create from data)
        auto emptyMgr = SkFontMgr::RefEmpty();
        return emptyMgr->makeFromData(data, ttcIndex);
    }
    
    sk_sp<SkTypeface> onMakeFromStreamIndex(std::unique_ptr<SkStreamAsset> stream,
                                            int ttcIndex) const override {
        // Not implemented - return nullptr
        (void)stream;
        (void)ttcIndex;
        return nullptr;
    }
    
    sk_sp<SkTypeface> onMakeFromStreamArgs(std::unique_ptr<SkStreamAsset> stream,
                                           const SkFontArguments& args) const override {
        // Not implemented - return nullptr
        (void)stream;
        (void)args;
        return nullptr;
    }
    
    sk_sp<SkTypeface> onMakeFromFile(const char* /* path */, int /* ttcIndex */) const override {
        return nullptr;
    }
    
    sk_sp<SkTypeface> onLegacyMakeTypeface(const char familyName[], SkFontStyle style) const override {
        return onMatchFamilyStyle(familyName, style);
    }
    
private:
    class SingleFontStyleSet : public SkFontStyleSet {
    public:
        SingleFontStyleSet(sk_sp<SkTypeface> tf) : typeface_(tf) {}
        
        int count() override { return 1; }
        
        void getStyle(int index, SkFontStyle* style, SkString* name) override {
            if (index == 0 && typeface_) {
                *style = typeface_->fontStyle();
                if (name) {
                    typeface_->getFamilyName(name);
                }
            }
        }
        
        sk_sp<SkTypeface> createTypeface(int index) override {
            return index == 0 ? typeface_ : nullptr;
        }
        
        sk_sp<SkTypeface> matchStyle(const SkFontStyle& /* style */) override {
            return typeface_;
        }
        
    private:
        sk_sp<SkTypeface> typeface_;
    };
    
    std::map<std::string, sk_sp<SkData>> fontData_;  // Store font data by name
    std::map<std::string, sk_sp<SkTypeface>> fonts_;  // By font name (e.g., "OpenSans-Bold")
    std::map<std::string, sk_sp<SkTypeface>> familyFonts_;  // By family name (e.g., "Open Sans")
};

// WASM-compatible animation context
struct WasmAnimationContext {
    sk_sp<skottie::Animation> animation;
    skottie::Animation::Builder builder;
    std::string processed_json;
    int width;
    int height;
    float duration;
    float fps;
    sk_sp<CustomFontMgr> fontMgr;
    
    WasmAnimationContext() : width(0), height(0), duration(0.0f), fps(30.0f) {
        // Create custom font manager
        fontMgr = sk_make_sp<CustomFontMgr>();
    }
};

// Global context
static std::unique_ptr<WasmAnimationContext> g_context;

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    int lotio_init(const char* json_data, size_t json_len, const char* layer_overrides_json, size_t layer_overrides_len, float textPadding, int textMeasurementModeInt) {
        try {
            // If context already exists (fonts were registered), reuse it
            // Otherwise create a new one
            if (!g_context) {
                g_context = std::make_unique<WasmAnimationContext>();
            }
            
            // Process JSON
            g_context->processed_json = std::string(json_data, json_len);
            normalizeLottieTextNewlines(g_context->processed_json);
            
            // Convert int to enum (0=FAST, 1=ACCURATE, 2=PIXEL_PERFECT)
            TextMeasurementMode textMeasurementMode = TextMeasurementMode::ACCURATE;  // Default
            if (textMeasurementModeInt == 0) {
                textMeasurementMode = TextMeasurementMode::FAST;
            } else if (textMeasurementModeInt == 1) {
                textMeasurementMode = TextMeasurementMode::ACCURATE;
            } else if (textMeasurementModeInt == 2) {
                textMeasurementMode = TextMeasurementMode::PIXEL_PERFECT;
            }
            
            if (layer_overrides_json && layer_overrides_len > 0) {
                std::string layer_overrides(layer_overrides_json, layer_overrides_len);
                // Pass the font manager with registered fonts for proper text measurement
                processLayerOverridesFromString(g_context->processed_json, layer_overrides, g_context->fontMgr.get(), textPadding, textMeasurementMode);
            }
            
            // Register codecs needed by SkResources for image decoding
            SkCodecs::Register(SkPngDecoder::Decoder());
            EM_ASM({
                console.log('[DEBUG] Registered image codecs: PNG decoder ready');
            });
            
            // Resource provider - use DataURI for WASM (handles embedded images)
            // DataURIResourceProviderProxy can handle data URIs in the JSON
            EM_ASM({
                console.log('[DEBUG] Creating DataURIResourceProvider for image loading...');
            });
            auto resourceProvider = skresources::DataURIResourceProviderProxy::Make(nullptr);
            if (resourceProvider) {
                g_context->builder.setResourceProvider(std::move(resourceProvider));
                EM_ASM({
                    console.log('[DEBUG] DataURIResourceProvider set successfully - images will be loaded from data URIs');
                });
            } else {
                EM_ASM({
                    console.warn('[WARNING] Failed to create DataURIResourceProvider - images may fail to load');
                });
            }
            
            // Font manager - use the one with registered fonts (or empty if none)
            g_context->builder.setFontManager(g_context->fontMgr);
            
            // Create animation
            EM_ASM({
                console.log('[DEBUG] Parsing animation JSON (this will load and decode images if present)...');
            });
            g_context->animation = g_context->builder.make(
                g_context->processed_json.c_str(), 
                g_context->processed_json.length()
            );
            
            if (!g_context->animation) {
                EM_ASM({
                    console.error('[ERROR] Failed to parse Lottie animation from JSON');
                    console.error('[ERROR] Possible causes: invalid JSON, missing image files, or unsupported features');
                });
                return 1;
            }
            
            // Get animation properties
            SkSize size = g_context->animation->size();
            g_context->width = static_cast<int>(size.width());
            g_context->height = static_cast<int>(size.height());
            g_context->duration = g_context->animation->duration();
            g_context->fps = g_context->animation->fps();
            
            // Debug: Log animation info
            // Note: We can't use std::cout in WASM, but we can use emscripten_log
            EM_ASM({
                console.log('[DEBUG] Animation created successfully: size=' + $0 + 'x' + $1 + ', duration=' + $2 + ', fps=' + $3);
                console.log('[DEBUG] Images should be loaded and ready for rendering');
            }, g_context->width, g_context->height, g_context->duration, g_context->fps);
            
            // Debug: Check if animation has inPoint/outPoint
            // Some animations might have no visible content if inPoint >= outPoint
            // (Commented out to avoid unused variable warning)
            // float inPoint = g_context->animation->inPoint();
            // float outPoint = g_context->animation->outPoint();
            
            return 0;
        } catch (...) {
            return 1;
        }
    }
    
    // Register a font from JavaScript
    // fontName: e.g., "OpenSans-Bold" (must match fName in Lottie JSON)
    // fontData: pointer to font file data (TTF/OTF)
    // fontDataSize: size of font data in bytes
    // NOTE: This should be called BEFORE lotio_init, but we also support calling it after
    // If called after, the font manager is already set, so we update it
    EMSCRIPTEN_KEEPALIVE
    int lotio_register_font(const char* fontName, const uint8_t* fontData, size_t fontDataSize) {
        if (!fontName || !fontData || fontDataSize == 0) {
            return 2;  // Invalid parameters
        }
        
        try {
            // Create SkData from font data
            sk_sp<SkData> data = SkData::MakeWithCopy(fontData, fontDataSize);
            if (!data) {
                return 3;  // Failed to create SkData
            }
            
            // If context doesn't exist yet, create it just for font registration
            // The font manager will be reused when lotio_init is called
            if (!g_context) {
                g_context = std::make_unique<WasmAnimationContext>();
            }
            
            // Register font with custom font manager
            g_context->fontMgr->registerFont(std::string(fontName), data);
            
            return 0;
        } catch (...) {
            return 4;  // Exception
        }
    }
    
    EMSCRIPTEN_KEEPALIVE
    int lotio_get_info(int* width, int* height, float* duration, float* fps) {
        if (!g_context || !g_context->animation) {
            return 1;
        }
        *width = g_context->width;
        *height = g_context->height;
        *duration = g_context->duration;
        *fps = g_context->fps;
        return 0;
    }
    
    EMSCRIPTEN_KEEPALIVE
    int lotio_render_frame(float time, uint8_t* rgba_out, size_t rgba_size) {
        if (!g_context || !g_context->animation) {
            return 1;
        }
        
        SkImageInfo info = SkImageInfo::MakeN32(
            g_context->width, 
            g_context->height, 
            kUnpremul_SkAlphaType
        );
        
        size_t rowBytes = info.minRowBytes();
        size_t expected_size = info.computeByteSize(rowBytes);
        if (rgba_size < expected_size) {
            return 2;
        }
        
        // Use Raster surface (like the native renderer does) instead of WrapPixels
        // This ensures proper rendering in WASM
        auto surface = SkSurfaces::Raster(info);
        if (!surface) {
            return 3;
        }
        
        // Clear to transparent background
        surface->getCanvas()->clear(SK_ColorTRANSPARENT);
        
        // Seek to the desired frame time
        g_context->animation->seekFrameTime(time);
        
        // Debug: Check animation bounds and inPoint/outPoint
        float inPoint = g_context->animation->inPoint();
        float outPoint = g_context->animation->outPoint();
        if (time < inPoint || time > outPoint) {
            // Time is outside animation range - render nothing
            EM_ASM({
                console.warn('Time ' + $0 + ' is outside animation range [' + $1 + ', ' + $2 + ']');
            }, time, inPoint, outPoint);
            // Still try to render - some animations might work
        }
        
        // Render the animation frame
        g_context->animation->render(surface->getCanvas());
        
        // Debug: Check if anything was drawn by sampling a few pixels
        // This is a quick check - if the surface is still all transparent, rendering might have failed
        EM_ASM({
            // Sample a few pixels to see if anything was drawn
            // We'll check this in JavaScript after readPixels
        });
        
        // Get the rendered image
        sk_sp<SkImage> image = surface->makeImageSnapshot();
        if (!image) {
            return 4;
        }
        
        // Get image info
        SkImageInfo imgInfo = image->imageInfo();
        
        // Convert to RGBA_8888 with kUnpremul_SkAlphaType if needed (like native renderer does)
        bool needs_conversion = (imgInfo.colorType() != kN32_SkColorType || 
                                 imgInfo.alphaType() != kUnpremul_SkAlphaType);
        
        if (needs_conversion) {
            // Create a conversion surface with the exact format we need
            auto convertSurface = SkSurfaces::Raster(info);
            if (!convertSurface) {
                return 6;
            }
            convertSurface->getCanvas()->clear(SK_ColorTRANSPARENT);
            convertSurface->getCanvas()->drawImage(image, 0, 0, SkSamplingOptions());
            image = convertSurface->makeImageSnapshot();
            if (!image) {
                return 7;
            }
        }
        
        // Now read pixels - use the exact format we created
        bool success = image->readPixels(
            info,
            rgba_out,
            rowBytes,
            0, 0
        );
        
        if (!success) {
            return 5;
        }
        
        return 0;
    }
    
    EMSCRIPTEN_KEEPALIVE
    int lotio_render_frame_png(float time, uint8_t* png_out, size_t* png_size, size_t max_size) {
        if (!g_context || !g_context->animation) {
            return 1;
        }
        
        SkImageInfo info = SkImageInfo::MakeN32(
            g_context->width, 
            g_context->height, 
            kUnpremul_SkAlphaType
        );
        auto surface = SkSurfaces::Raster(info);
        if (!surface) {
            return 2;
        }
        
        surface->getCanvas()->clear(SK_ColorTRANSPARENT);
        g_context->animation->seekFrameTime(time);
        g_context->animation->render(surface->getCanvas());
        
        sk_sp<SkImage> image = surface->makeImageSnapshot();
        if (!image) {
            return 3;
        }
        
        SkPngEncoder::Options png_options;
        png_options.fZLibLevel = 1;
        sk_sp<SkData> png_data = SkPngEncoder::Encode(nullptr, image.get(), png_options);
        
        if (!png_data || png_data->size() > max_size) {
            return 4;
        }
        
        memcpy(png_out, png_data->data(), png_data->size());
        *png_size = png_data->size();
        
        return 0;
    }
    
    EMSCRIPTEN_KEEPALIVE
    void lotio_cleanup() {
        g_context.reset();
    }
    
    // Version function - returns version string
    EMSCRIPTEN_KEEPALIVE
    const char* lotio_get_version() {
        #ifdef VERSION
        return VERSION;
        #else
        // Fallback: use build-time date/time macros (compile-time constants)
        // Note: This fallback should rarely be used as build scripts always define VERSION
        // Format: "dev-MMM DD YYYY-HH:MM:SS" (e.g., "dev-Jan 01 2024-12:00:00")
        // Build scripts generate "dev-YYYYMMDD-HHMMSS" format when VERSION is not set
        return "dev-" __DATE__ "-" __TIME__;
        #endif
    }
}

