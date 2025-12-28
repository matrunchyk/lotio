#ifndef JSON_MANIPULATION_H
#define JSON_MANIPULATION_H

#include <string>

// Adjust text animator position keyframes based on text width change
// For right-aligned text, when text is wider, we need to move it further left (more negative X)
void adjustTextAnimatorPosition(
    std::string& json,
    const std::string& layerName,
    float widthDiff
);

// Modify JSON to update text layer
void modifyTextLayerInJson(
    std::string& json,
    const std::string& layerName,
    const std::string& newText,
    float newSize
);

// Normalize Lottie text newlines
void normalizeLottieTextNewlines(std::string& json);

#endif // JSON_MANIPULATION_H

