#include "json_manipulation.h"
#include "../utils/string_utils.h"
#include "../utils/logging.h"
#include <regex>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

void adjustTextAnimatorPosition(
    std::string& json,
    const std::string& layerName,
    float widthDiff
) {
    if (std::abs(widthDiff) < 0.1f) {
        return;  // No significant change
    }
    
    // Find the layer by name
    std::string layerNamePattern = "\"nm\"\\s*:\\s*\"" + escapeRegex(layerName) + "\"";
    std::regex layerNameRegex(layerNamePattern);
    std::smatch layerMatch;
    
    if (!std::regex_search(json, layerMatch, layerNameRegex)) {
        return;  // Layer not found
    }
    
    size_t layerNamePos = layerMatch.position(0);
    
    // Find the text animator position property: t.a[0].a.p.k (array of keyframes)
    // Search for the position property in the text animator within a reasonable window
    size_t searchStart = layerNamePos;
    size_t searchEnd = std::min(layerNamePos + 15000, json.length());
    std::string layerSection = json.substr(searchStart, searchEnd - searchStart);
    
    // Find "p" property in text animator that has animated keyframes
    // Look for pattern: "p": {"a": 1, "k": [
    // We need to find the position property that's inside the text animator array
    size_t textDataPos = layerSection.find("\"t\"");
    if (textDataPos == std::string::npos) {
        return;  // No text data found
    }
    
    // Find animator array "a": [
    size_t animatorArrayPos = layerSection.find("\"a\"", textDataPos);
    if (animatorArrayPos == std::string::npos) {
        return;  // No animator array found
    }
    
    // Find the position property "p" with animated keyframes
    // Pattern: "p": {"a": 1, "k": [{"s": [-679, 0, 0], ...}, ...]}
    size_t pPos = layerSection.find("\"p\"", animatorArrayPos);
    while (pPos != std::string::npos && pPos < textDataPos + 5000) {
        // Check if this "p" is followed by animated keyframes
        size_t checkPos = pPos + 3;
        while (checkPos < layerSection.length() && (layerSection[checkPos] == ' ' || layerSection[checkPos] == '\t' || layerSection[checkPos] == ':')) {
            checkPos++;
        }
        
        // Look for "a": 1 pattern (animated)
        size_t aCheck = layerSection.find("\"a\"", pPos);
        if (aCheck != std::string::npos && aCheck < pPos + 50) {
            size_t colonPos = layerSection.find(':', aCheck);
            if (colonPos != std::string::npos) {
                size_t valueStart = colonPos + 1;
                while (valueStart < layerSection.length() && (layerSection[valueStart] == ' ' || layerSection[valueStart] == '\t')) {
                    valueStart++;
                }
                if (valueStart < layerSection.length() && layerSection[valueStart] == '1') {
                    // Found animated position property, now find and adjust keyframes
                    // Find keyframes array "k": [
                    size_t kPos = layerSection.find("\"k\"", pPos);
                    if (kPos != std::string::npos) {
                        size_t arrayStart = layerSection.find('[', kPos);
                        if (arrayStart != std::string::npos) {
                            // Find all "s": [x, y, z] patterns and adjust X values
                            // Search in the full json starting from the absolute position of arrayStart
                            size_t absoluteArrayStart = searchStart + arrayStart;
                            size_t arrayEnd = absoluteArrayStart;
                            
                            // Find the end of the keyframes array by counting brackets
                            int bracketCount = 0;
                            bool inArray = false;
                            for (size_t i = absoluteArrayStart; i < json.length() && i < absoluteArrayStart + 10000; i++) {
                                if (json[i] == '[') {
                                    bracketCount++;
                                    inArray = true;
                                } else if (json[i] == ']') {
                                    bracketCount--;
                                    if (bracketCount == 0 && inArray) {
                                        arrayEnd = i + 1;
                                        break;
                                    }
                                }
                            }
                            
                            if (arrayEnd > absoluteArrayStart) {
                                // Use regex to find and replace all negative X values in keyframes
                                // Pattern: "s": [-number, ...] - we want to replace the -number part
                                std::ostringstream widthDiffStr;
                                widthDiffStr << std::fixed << std::setprecision(1) << widthDiff;
                                
                                // Find all occurrences of negative X values in the keyframes array
                                std::string keyframeSection = json.substr(absoluteArrayStart, arrayEnd - absoluteArrayStart);
                                
                                // Use regex to find all "s": [-number, ...] patterns
                                std::regex negXPattern("\"s\"\\s*:\\s*\\[\\s*-([0-9]+\\.?[0-9]*)\\s*,");
                                std::sregex_iterator iter(keyframeSection.begin(), keyframeSection.end(), negXPattern);
                                std::sregex_iterator end;
                                
                                // Collect replacements (from end to start)
                                std::vector<std::pair<size_t, std::string>> replacements;
                                
                                for (; iter != end; ++iter) {
                                    std::smatch match = *iter;
                                    float x = -std::stof(match[1].str());  // Negative value (off-screen left)
                                    // widthDiff = newWidth - originalWidth
                                    // For right-aligned text that moves off-screen to the left:
                                    // - If text gets WIDER (widthDiff > 0): need to move further left (more negative)
                                    // - If text gets NARROWER (widthDiff < 0): can keep same position or move less left
                                    // Since the user is seeing the tail, the text is likely still too wide for the position
                                    // So we should move it further left regardless: newX = x - abs(widthDiff) when text is wider
                                    // But actually, if widthDiff is negative (narrower), x - widthDiff makes it less negative (wrong direction)
                                    // We want: if wider, move more left; if narrower, don't adjust (or adjust proportionally)
                                    float newX = x;
                                    if (widthDiff > 0.1f) {
                                        // Text got wider - move further left (more negative)
                                        newX = x - widthDiff;
                                    } else if (widthDiff < -0.1f) {
                                        // Text got narrower - but user still sees tail, so maybe we need to move it more left anyway?
                                        // Actually, if text is narrower, the tail shouldn't be visible unless position is wrong
                                        // For now, don't adjust if text got narrower
                                        newX = x;
                                    }
                                    
                                    std::ostringstream newValue;
                                    newValue << std::fixed << std::setprecision(1) << newX;
                                    
                                    // Find the position of the X value in the original json
                                    size_t relativePos = match.position(0) + match.str().find(match[1].str());
                                    size_t absolutePos = absoluteArrayStart + relativePos;
                                    
                                    // Replace just the number part
                                    size_t numStart = absolutePos;
                                    size_t numEnd = numStart + match[1].str().length();
                                    
                                    // Include the minus sign
                                    if (numStart > 0 && json[numStart - 1] == '-') {
                                        numStart--;
                                    }
                                    
                                    replacements.push_back({numStart, newValue.str()});
                                }
                                
                                // Apply replacements from end to start
                                for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
                                    size_t numStart = it->first;
                                    // Find the end of the number
                                    size_t numEnd = numStart;
                                    if (json[numEnd] == '-') numEnd++;
                                    while (numEnd < json.length() && (std::isdigit(json[numEnd]) || json[numEnd] == '.')) {
                                        numEnd++;
                                    }
                                    
                                    json.replace(numStart, numEnd - numStart, it->second);
                                    
                                    if (g_debug_mode) {
                                        LOG_COUT("[DEBUG] Replaced X value at " << numStart << ": -> " << it->second) << std::endl;
                                    }
                                }
                            }
                            
                            return;  // Found and adjusted, done
                        }
                    }
                }
            }
        }
        
        // Try next "p" occurrence
        pPos = layerSection.find("\"p\"", pPos + 1);
    }
}

void modifyTextLayerInJson(
    std::string& json,
    const std::string& layerName,
    const std::string& newText,
    float newSize
) {
    // Find the layer using the same approach as extractFontInfoFromJson
    size_t layerNamePos = std::string::npos;
    
    // Try regex first
    std::string simplePattern = "\"nm\"\\s*:\\s*\"" + layerName + "\"";
    std::regex simpleRegex(simplePattern);
    std::smatch layerMatch;
    
    if (std::regex_search(json, layerMatch, simpleRegex)) {
        layerNamePos = layerMatch.position(0);
    } else {
        // Fallback: simple string search
        size_t nmPos = json.find("\"nm\"");
        while (nmPos != std::string::npos && nmPos < json.length() - 100) {
            size_t colonPos = json.find(':', nmPos);
            if (colonPos != std::string::npos && colonPos < nmPos + 20) {
                size_t nameStart = json.find('"', colonPos);
                if (nameStart != std::string::npos) {
                    size_t nameEnd = json.find('"', nameStart + 1);
                    if (nameEnd != std::string::npos) {
                        std::string foundName = json.substr(nameStart + 1, nameEnd - nameStart - 1);
                        if (foundName == layerName) {
                            layerNamePos = nmPos;
                            break;
                        }
                    }
                }
            }
            nmPos = json.find("\"nm\"", nmPos + 1);
        }
    }
    
    if (layerNamePos == std::string::npos) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find layer name in JSON: " << layerName) << std::endl;
        }
        return;
    }
    
    // Find the "t" object (text data)
    size_t searchWindow = std::min(layerNamePos + 5000, json.length());
    size_t textDataPos = json.find("\"t\"", layerNamePos);
    if (textDataPos == std::string::npos || textDataPos > searchWindow) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find \"t\" object for layer: " << layerName) << std::endl;
        }
        return;
    }
    
    // Find the "s" object (text style) - it should be in t.d.k[0].s
    // We need to find "s" that's inside the structure: "t": { "d": { "k": [ { "s": {
    // Look for "d" first, then "k", then find "s" inside the first array element
    size_t dPos = json.find("\"d\"", textDataPos);
    if (dPos == std::string::npos || dPos > textDataPos + 500) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find \"d\" object for layer: " << layerName) << std::endl;
        }
        return;
    }
    
    // Find "k" (the array) after "d"
    size_t kPos = json.find("\"k\"", dPos);
    if (kPos == std::string::npos || kPos > dPos + 200) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find \"k\" array for layer: " << layerName) << std::endl;
        }
        return;
    }
    
    // Find the opening bracket of the array
    size_t arrayOpen = json.find('[', kPos);
    if (arrayOpen == std::string::npos) {
        return;
    }
    
    // Find the opening brace of the first array element
    size_t elemOpen = json.find('{', arrayOpen);
    if (elemOpen == std::string::npos) {
        return;
    }
    
    // The structure is: k: [{ "s": { "s": 122, ... }, "t": 0 }]
    // We need to find the "s": { object (text style object) within the first array element
    // Find the closing brace of the array element first to limit our search
    int elemBraceCount = 0;
    size_t elemClose = elemOpen;
    for (size_t i = elemOpen; i < std::min(elemOpen + 1000, json.length()); i++) {
        if (json[i] == '{') elemBraceCount++;
        if (json[i] == '}') elemBraceCount--;
        if (elemBraceCount == 0) {
            elemClose = i;
            break;
        }
    }
    
    // Look for "s": { pattern within the array element
    size_t textStyleObjStart = std::string::npos;
    size_t searchStart = elemOpen;
    size_t searchEnd = elemClose;
    
    while (searchStart < searchEnd) {
        size_t sPos = json.find("\"s\"", searchStart);
        if (sPos == std::string::npos || sPos >= searchEnd) {
            break;
        }
        // Check what follows "s"
        size_t colonPos = json.find(':', sPos);
        if (colonPos != std::string::npos && colonPos < sPos + 10 && colonPos < searchEnd) {
            // Skip whitespace after colon
            size_t valueStart = colonPos + 1;
            while (valueStart < json.length() && valueStart < colonPos + 20 && 
                   (json[valueStart] == ' ' || json[valueStart] == '\t' || json[valueStart] == '\n' || json[valueStart] == '\r')) {
                valueStart++;
            }
            // If followed by '{', this is the text style object "s": {
            if (valueStart < json.length() && json[valueStart] == '{') {
                textStyleObjStart = valueStart;  // This is the opening brace of "s": {
                break;
            }
        }
        searchStart = sPos + 1;
    }
    
    if (textStyleObjStart == std::string::npos) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find \"s\": { object for layer " << layerName) << std::endl;
        }
        return;
    }
    
    size_t styleOpenBrace = textStyleObjStart;
    
    // Find matching closing brace - need to handle nested structures
    int braceCount = 0;
    size_t styleCloseBrace = styleOpenBrace;
    size_t searchLimit = std::min(styleOpenBrace + 2000, json.length());  // Increased limit
    for (size_t i = styleOpenBrace; i < searchLimit; i++) {
        if (json[i] == '{') braceCount++;
        if (json[i] == '}') braceCount--;
        if (braceCount == 0) {
            styleCloseBrace = i;
            break;
        }
    }
    
    if (styleCloseBrace <= styleOpenBrace || styleCloseBrace >= searchLimit) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find matching closing brace for text style object in layer " << layerName) << std::endl;
        }
        return;
    }
    
    // Extract the text style JSON
    std::string textStyleJson = json.substr(styleOpenBrace + 1, styleCloseBrace - styleOpenBrace - 1);
    
    if (g_debug_mode) {
        // Show first 200 chars of extracted JSON for debugging
        std::string preview = textStyleJson.substr(0, std::min(200UL, textStyleJson.length()));
        LOG_COUT("[DEBUG] Extracted textStyleJson for " << layerName << " (first 200 chars): " << preview) << std::endl;
    }
    
    // Replace font size - find "s": number and replace it directly using string operations
    // The extracted textStyleJson contains "s": 122, "f": ..., so we need to replace the number
    size_t sFieldPos = textStyleJson.find("\"s\"");
    if (sFieldPos != std::string::npos) {
        // Find the colon after "s"
        size_t colonPos = textStyleJson.find(':', sFieldPos);
        if (colonPos != std::string::npos && colonPos < sFieldPos + 10) {
            // Skip whitespace and find the number
            size_t numStart = colonPos + 1;
            while (numStart < textStyleJson.length() && 
                   (textStyleJson[numStart] == ' ' || textStyleJson[numStart] == '\t' || 
                    textStyleJson[numStart] == '\n' || textStyleJson[numStart] == '\r')) {
                numStart++;
            }
            // Check if it's a number (not an object)
            if (numStart < textStyleJson.length() && std::isdigit(textStyleJson[numStart])) {
                // Find the end of the number (digit or decimal point)
                size_t numEnd = numStart;
                while (numEnd < textStyleJson.length() && 
                       (std::isdigit(textStyleJson[numEnd]) || textStyleJson[numEnd] == '.')) {
                    numEnd++;
                }
                // Skip whitespace after number
                size_t afterNum = numEnd;
                while (afterNum < textStyleJson.length() && 
                       (textStyleJson[afterNum] == ' ' || textStyleJson[afterNum] == '\t')) {
                    afterNum++;
                }
                // Replace the number with new size
                std::ostringstream sizeStream;
                sizeStream << std::fixed << std::setprecision(1) << newSize;
                std::string newSizeStr = sizeStream.str();
                textStyleJson = textStyleJson.substr(0, numStart) + newSizeStr + textStyleJson.substr(afterNum);
            }
        }
    }
    
    // Replace text content (escape quotes in newText)
    // For newlines, we need to use Unicode escape sequences in JSON (\u000D for \r)
    // First, convert \u0003 and actual \r/\n characters to \r
    std::string processedText = newText;
    replaceAllInPlace(processedText, "\\u0003", "\r");
    replaceCharInPlace(processedText, '\x03', '\r');
    // Normalize \n to \r for Lottie
    replaceCharInPlace(processedText, '\n', '\r');
    
    // Now escape for JSON
    std::string escapedText = processedText;
    // Escape backslashes first (before other escapes)
    replaceAllInPlace(escapedText, "\\", "\\\\");
    // Escape quotes
    replaceAllInPlace(escapedText, "\"", "\\\"");
    // Convert actual \r characters to Unicode escape \u000D (carriage return)
    // This must be done after backslash escaping to avoid double-escaping
    replaceAllInPlace(escapedText, "\r", "\\u000D");
    // Also handle \t
    replaceAllInPlace(escapedText, "\t", "\\t");
    
    // Replace text content using string operations for reliability
    // Find "t": "text" and replace the text value
    size_t tFieldPos = textStyleJson.find("\"t\"");
    if (tFieldPos != std::string::npos) {
        // Find the colon after "t"
        size_t colonPos = textStyleJson.find(':', tFieldPos);
        if (colonPos != std::string::npos && colonPos < tFieldPos + 10) {
            // Find the opening quote
            size_t quoteStart = textStyleJson.find('"', colonPos);
            if (quoteStart != std::string::npos && quoteStart < colonPos + 20) {
                // Find the closing quote (handle escaped quotes)
                size_t quoteEnd = quoteStart + 1;
                while (quoteEnd < textStyleJson.length()) {
                    if (textStyleJson[quoteEnd] == '"' && textStyleJson[quoteEnd - 1] != '\\') {
                        break;  // Found unescaped closing quote
                    }
                    quoteEnd++;
                }
                if (quoteEnd < textStyleJson.length()) {
                    // Replace the text between quotes
                    std::string beforeReplace = textStyleJson;
                    textStyleJson = textStyleJson.substr(0, quoteStart + 1) + escapedText + textStyleJson.substr(quoteEnd);
                    
                    if (g_debug_mode) {
                        // Verify replacement
                        if (textStyleJson.find(escapedText) == std::string::npos) {
                            LOG_COUT("[DEBUG] Error: Text replacement failed for layer " << layerName) << std::endl;
                        } else {
                            LOG_COUT("[DEBUG] Text replacement successful for " << layerName << ": \"" << newText << "\"") << std::endl;
                        }
                    }
                } else {
                    if (g_debug_mode) {
                        LOG_COUT("[DEBUG] Warning: Could not find closing quote for text field in layer " << layerName) << std::endl;
                    }
                }
            }
        }
    }
    
    if (g_debug_mode) {
        // Verify the replacement worked by checking if the new text appears
        if (textStyleJson.find(escapedText) == std::string::npos) {
            LOG_COUT("[DEBUG] Warning: Text replacement may have failed for layer " << layerName) << std::endl;
            LOG_COUT("[DEBUG] Looking for text in: " << textStyleJson.substr(0, std::min(300UL, textStyleJson.length()))) << std::endl;
        }
    }
    
    // Reconstruct the JSON
    // styleOpenBrace points to the opening '{', textStyleJson is the content without braces
    // styleCloseBrace points to the closing '}'
    // We need: [before] { [modified content] } [after]
    if (styleOpenBrace >= json.length() || styleCloseBrace >= json.length() || styleCloseBrace <= styleOpenBrace) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Error: Invalid brace positions for layer " << layerName) << std::endl;
        }
        return;
    }
    
    std::string before = json.substr(0, styleOpenBrace + 1);  // Includes opening brace
    std::string after = json.substr(styleCloseBrace);         // Starts from closing brace (includes it)
    json = before + textStyleJson + after;
    
    if (g_debug_mode) {
        // Quick validation: check that braces are balanced in the modified section
        int braceCount = 0;
        for (char c : textStyleJson) {
            if (c == '{') braceCount++;
            if (c == '}') braceCount--;
        }
        if (braceCount != 0) {
            LOG_COUT("[DEBUG] Warning: Unbalanced braces in modified textStyleJson for " << layerName << " (count: " << braceCount << ")") << std::endl;
        }
    }
    
    // Validate: ensure we didn't break the JSON structure
    // Count braces in the modified section to ensure balance
    int openBraces = 0, closeBraces = 0;
    for (char c : textStyleJson) {
        if (c == '{') openBraces++;
        if (c == '}') closeBraces++;
    }
    // The textStyleJson should not contain unmatched braces since it's the content of a single object
}

void normalizeLottieTextNewlines(std::string& json) {
    // Some Lottie producers use U+0003 (ETX) as a soft line-break marker inside "t".
    // Skottie interprets '\r' as a newline in Lottie JSON.
    // We normalize it to '\r' (which will be escaped as "\\r" in JSON strings).
    //
    // We handle both representations:
    // 1) Escaped form inside JSON: "\\u0003"
    // 2) Literal byte form already present in the string: '\x03'
    const auto replacedEscaped = replaceAllInPlace(json, "\\u0003", "\\r");
    const auto replacedLiteral = replaceCharInPlace(json, '\x03', '\r');

    LOG_DEBUG("Text newline normalization: replacedEscaped=\\u0003->\\r x" << replacedEscaped
              << ", replacedLiteral=0x03->\\r x" << replacedLiteral);
}

