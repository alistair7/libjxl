// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "tools/comparison_viewer/image_loading.h"

#include <jxl/cms.h>

#include <QRgb>
#include <QThread>
#include <cstdint>
#include <vector>

#include "lib/extras/codec.h"
#include "lib/extras/dec/color_hints.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/rect.h"
#include "lib/jxl/image_bundle.h"
#include "lib/jxl/image_metadata.h"
#include "tools/file_io.h"
#include "tools/no_memory_manager.h"
#include "tools/thread_pool_internal.h"
#include "tools/viewer/load_jxl.h"

namespace jpegxl {
namespace tools {

using ::jxl::CodecInOut;
using ::jxl::ColorEncoding;
using ::jxl::Image3F;
using ::jxl::ImageBundle;
using ::jxl::ImageF;
using ::jxl::Rect;
using ::jxl::Span;
using ::jxl::Status;
using ::jxl::ThreadPool;
using ::jxl::extras::ColorHints;

using IccBytes = std::vector<uint8_t>;

namespace {

Status loadFromFile(const QString& filename, const ColorHints& color_hints,
                    CodecInOut* const decoded, ThreadPool* const pool) {
  std::vector<uint8_t> compressed;
  JXL_RETURN_IF_ERROR(
      jpegxl::tools::ReadFile(filename.toStdString(), &compressed));
  const Span<const uint8_t> compressed_span(compressed);
  return jxl::SetFromBytes(compressed_span, color_hints, decoded, pool,
                           nullptr);
}

}  // namespace

bool canLoadImageWithExtension(QString extension) {
  extension = extension.toLower();
  if (extension == "jxl" || extension == "j" || extension == "brn") {
    return true;
  }
  const auto codec = jxl::extras::CodecFromPath("." + extension.toStdString());
  return codec != jxl::extras::Codec::kUnknown;
}

QImage loadImage(const QString& filename, const QByteArray& targetIccProfile,
                 const float intensityTarget,
                 const QString& sourceColorSpaceHint) {
  qint64 elapsed;
  QImage img = loadJxlImage(filename, targetIccProfile, &elapsed);
  if (img.width() != 0 && img.height() != 0) {
    return img;
  }
  static ThreadPoolInternal pool(QThread::idealThreadCount());

  auto decoded = jxl::make_unique<CodecInOut>(jpegxl::tools::NoMemoryManager());
  ColorHints color_hints;
  if (!sourceColorSpaceHint.isEmpty()) {
    color_hints.Add("color_space", sourceColorSpaceHint.toStdString());
  }
  if (!loadFromFile(filename, color_hints, decoded.get(), pool.get())) {
    return QImage();
  }
  decoded->metadata.m.SetIntensityTarget(intensityTarget);
  const ImageBundle& ib = decoded->Main();

  ColorEncoding targetColorSpace;
  bool use_fallback_profile = true;
  if (!targetIccProfile.isEmpty()) {
    IccBytes icc;
    icc.assign(reinterpret_cast<const uint8_t*>(targetIccProfile.data()),
               reinterpret_cast<const uint8_t*>(targetIccProfile.data() +
                                                targetIccProfile.size()));
    use_fallback_profile =
        !targetColorSpace.SetICC(std::move(icc), JxlGetDefaultCms());
  }
  if (use_fallback_profile) {
    targetColorSpace = ColorEncoding::SRGB(ib.IsGray());
  }
  Image3F converted;
  if (!ib.CopyTo(Rect{ib}, targetColorSpace, *JxlGetDefaultCms(), &converted,
                 pool.get())) {
    return QImage();
  }

  QImage image(converted.xsize(), converted.ysize(), QImage::Format_ARGB32);

  const auto ScaleAndClamp = [](const float x) {
    return jxl::Clamp1(x * 255 + .5f, 0.f, 255.f);
  };

  if (ib.HasAlpha()) {
    const ImageF* alpha = ib.alpha();
    for (int y = 0; y < image.height(); ++y) {
      QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
      const float* const alphaRow = alpha->ConstRow(y);
      const float* const redRow = converted.ConstPlaneRow(0, y);
      const float* const greenRow = converted.ConstPlaneRow(1, y);
      const float* const blueRow = converted.ConstPlaneRow(2, y);
      for (int x = 0; x < image.width(); ++x) {
        row[x] = qRgba(ScaleAndClamp(redRow[x]), ScaleAndClamp(greenRow[x]),
                       ScaleAndClamp(blueRow[x]), ScaleAndClamp(alphaRow[x]));
      }
    }
  } else {
    for (int y = 0; y < image.height(); ++y) {
      QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
      const float* const redRow = converted.ConstPlaneRow(0, y);
      const float* const greenRow = converted.ConstPlaneRow(1, y);
      const float* const blueRow = converted.ConstPlaneRow(2, y);
      for (int x = 0; x < image.width(); ++x) {
        row[x] = qRgb(ScaleAndClamp(redRow[x]), ScaleAndClamp(greenRow[x]),
                      ScaleAndClamp(blueRow[x]));
      }
    }
  }

  return image;
}

}  // namespace tools
}  // namespace jpegxl
