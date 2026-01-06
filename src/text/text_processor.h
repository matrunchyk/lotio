#ifndef TEXT_PROCESSOR_H
#define TEXT_PROCESSOR_H

#include <string>
#include "font_utils.h"

// Process JSON with layer overrides (text auto-fit, dynamic text values, and image path overrides)
// Returns processed JSON string, or empty string on error
// textPadding: padding factor (0.0-1.0), default 0.97 means 97% of target width (3% padding)
// textMeasurementMode: measurement accuracy mode (default: ACCURATE for good balance)
std::string processLayerOverrides(
    std::string& json_data,
    const std::string& layer_overrides_file,
    float textPadding = 0.97f,
    TextMeasurementMode textMeasurementMode = TextMeasurementMode::ACCURATE
);

#endif // TEXT_PROCESSOR_H

