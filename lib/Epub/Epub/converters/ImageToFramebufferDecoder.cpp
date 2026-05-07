#include "ImageToFramebufferDecoder.h"

#include <Logging.h>

bool ImageToFramebufferDecoder::validateImageDimensions(int width, int height, const std::string& format) {
  if (width <= 0 || height <= 0 || width > MAX_SOURCE_WIDTH || height > MAX_SOURCE_HEIGHT) {
    LOG_ERR("IMG", "Image too large or invalid (%dx%d %s), max supported: %dx%d", width, height, format.c_str(),
            MAX_SOURCE_WIDTH, MAX_SOURCE_HEIGHT);
    return false;
  }
  return true;
}

void ImageToFramebufferDecoder::warnUnsupportedFeature(const std::string& feature, const std::string& imagePath) {
  LOG_ERR("IMG", "Warning: Unsupported feature '%s' in image '%s'. Image may not display correctly.", feature.c_str(),
          imagePath.c_str());
}
