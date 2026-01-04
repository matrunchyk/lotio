#include "animation_setup.h"
#include "../utils/logging.h"
#include "../text/json_manipulation.h"
#include "../text/text_processor.h"
#include "modules/skresources/include/SkResources.h"
#include "include/codec/SkCodec.h"
#include "include/codec/SkPngDecoder.h"
#include "include/ports/SkFontScanner_FreeType.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include <fontconfig/fontconfig.h>
#include <fstream>
#include <filesystem>

// Read JSON file and apply text processing
static std::string readAndProcessJson(
    const std::string& input_file,
    const std::string& text_config_file,
    float textPadding,
    TextMeasurementMode textMeasurementMode
) {
    // Read Lottie JSON file
    std::ifstream file(input_file);
    if (!file.is_open()) {
        LOG_CERR("Error: Could not open input file: " << input_file) << std::endl;
        return "";
    }

    std::string json_data((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    file.close();

    // Register codecs needed by SkResources FileResourceProvider for image decoding.
    // (SkResources docs: clients must call SkCodec::Register() before using FileResourceProvider.)
    SkCodecs::Register(SkPngDecoder::Decoder());
    LOG_DEBUG("Registered image codecs via SkCodecs::Register: png");

    normalizeLottieTextNewlines(json_data);
    
    // Apply text configuration if config file is provided
    processTextConfiguration(json_data, text_config_file, textPadding, textMeasurementMode);
    
    return json_data;
}

AnimationSetupResult setupAndCreateAnimation(
    const std::string& input_file,
    const std::string& text_config_file,
    float textPadding,
    TextMeasurementMode textMeasurementMode
) {
    AnimationSetupResult result;
    
    // Read and process JSON (apply text config if needed)
    result.processed_json = readAndProcessJson(input_file, text_config_file, textPadding, textMeasurementMode);
    if (result.processed_json.empty()) {
        return result;  // animation will be nullptr
    }
    
    // Debug: save modified JSON to file for inspection
    if (g_debug_mode && !text_config_file.empty()) {
        std::ofstream debugFile("/workspace/modified_json_debug.json");
        if (debugFile.is_open()) {
            debugFile << result.processed_json;
            debugFile.close();
            LOG_DEBUG("Saved modified JSON to modified_json_debug.json for inspection");
        }
    }
    
    // Builder is already default-constructed in struct
    
    LOG_DEBUG("Creating Skottie animation...");
    LOG_DEBUG("JSON size: " << result.processed_json.length() << " bytes");
    
    // Resource provider (images, etc.)
    {
        std::filesystem::path jsonPath(input_file);
        std::filesystem::path baseDir = jsonPath.has_parent_path() ? jsonPath.parent_path()
                                                                   : std::filesystem::path(".");
        std::error_code ec;
        std::filesystem::path absBaseDir = std::filesystem::absolute(baseDir, ec);
        const auto baseDirStr = (ec ? baseDir.string() : absBaseDir.string());

        LOG_DEBUG("ResourceProvider base_dir: " << baseDirStr);
        auto fileRP = skresources::FileResourceProvider::Make(SkString(baseDirStr.c_str()),
                                                              skresources::ImageDecodeStrategy::kPreDecode);
        if (!fileRP) {
            LOG_CERR("[ERROR] Failed to create skresources::FileResourceProvider for base_dir=" << baseDirStr) << std::endl;
        } else {
            auto cachingRP = skresources::CachingResourceProvider::Make(std::move(fileRP));
            result.builder.setResourceProvider(std::move(cachingRP));
            LOG_DEBUG("ResourceProvider set (FileResourceProvider + CachingResourceProvider)");
        }
    }

    // Font manager: Use fontconfig (handles both system fonts and custom fonts via fontconfig)
    // Custom fonts in /usr/local/share/fonts should be registered via fc-cache
    LOG_DEBUG("Setting up font manager...");
    sk_sp<SkFontMgr> fontMgr;
    
    try {
        const auto fcInitOk = FcInit();
        LOG_DEBUG("FcInit() returned " << (fcInitOk ? "true" : "false"));

        auto scanner = SkFontScanner_Make_FreeType();
        if (!scanner) {
            LOG_CERR("[ERROR] SkFontScanner_Make_FreeType() returned nullptr; cannot use fontconfig") << std::endl;
            fontMgr = SkFontMgr::RefEmpty();
        } else {
            fontMgr = SkFontMgr_New_FontConfig(nullptr, std::move(scanner));
            if (fontMgr) {
                LOG_DEBUG("Fontconfig font manager created successfully");
                LOG_DEBUG("Fontconfig will find system fonts and custom fonts (if registered via fc-cache)");
            } else {
                LOG_CERR("[ERROR] Failed to create fontconfig font manager") << std::endl;
                fontMgr = SkFontMgr::RefEmpty();
            }
        }
    } catch (...) {
        LOG_CERR("[ERROR] Exception creating fontconfig font manager") << std::endl;
        fontMgr = SkFontMgr::RefEmpty();
    }
    
    result.builder.setFontManager(fontMgr);
    LOG_DEBUG("Font manager set on builder");

    LOG_DEBUG("Calling builder.make() to parse JSON...");
    result.animation = result.builder.make(result.processed_json.c_str(), result.processed_json.length());
    
    if (!result.animation) {
        LOG_CERR("[ERROR] Failed to parse Lottie animation from JSON") << std::endl;
        return result;  // animation will be nullptr
    }
    
    LOG_DEBUG("Animation parsed successfully");
    return result;
}

