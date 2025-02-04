/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/image-decoders/webp/WEBPImageDecoder.h"

#if USE(QCMSLIB)
#include "qcms.h"
#endif

#if CPU(BIG_ENDIAN) || CPU(MIDDLE_ENDIAN)
#error Blink assumes a little-endian target.
#endif

#if SK_B32_SHIFT  // Output little-endian RGBA pixels (Android).
inline WEBP_CSP_MODE outputMode(bool hasAlpha) {
  return hasAlpha ? MODE_rgbA : MODE_RGBA;
}
#else  // Output little-endian BGRA pixels.
inline WEBP_CSP_MODE outputMode(bool hasAlpha) {
  return hasAlpha ? MODE_bgrA : MODE_BGRA;
}
#endif

inline uint8_t blendChannel(uint8_t src,
                            uint8_t srcA,
                            uint8_t dst,
                            uint8_t dstA,
                            unsigned scale) {
  unsigned blendUnscaled = src * srcA + dst * dstA;
  ASSERT(blendUnscaled < (1ULL << 32) / scale);
  return (blendUnscaled * scale) >> 24;
}

inline uint32_t blendSrcOverDstNonPremultiplied(uint32_t src, uint32_t dst) {
  uint8_t srcA = SkGetPackedA32(src);
  if (srcA == 0)
    return dst;

  uint8_t dstA = SkGetPackedA32(dst);
  uint8_t dstFactorA = (dstA * SkAlpha255To256(255 - srcA)) >> 8;
  ASSERT(srcA + dstFactorA < (1U << 8));
  uint8_t blendA = srcA + dstFactorA;
  unsigned scale = (1UL << 24) / blendA;

  uint8_t blendR = blendChannel(SkGetPackedR32(src), srcA, SkGetPackedR32(dst),
                                dstFactorA, scale);
  uint8_t blendG = blendChannel(SkGetPackedG32(src), srcA, SkGetPackedG32(dst),
                                dstFactorA, scale);
  uint8_t blendB = blendChannel(SkGetPackedB32(src), srcA, SkGetPackedB32(dst),
                                dstFactorA, scale);

  return SkPackARGB32NoCheck(blendA, blendR, blendG, blendB);
}

// Returns two point ranges (<left, width> pairs) at row |canvasY| which belong
// to |src| but not |dst|. A range is empty if its width is 0.
inline void findBlendRangeAtRow(const blink::IntRect& src,
                                const blink::IntRect& dst,
                                int canvasY,
                                int& left1,
                                int& width1,
                                int& left2,
                                int& width2) {
  ASSERT_WITH_SECURITY_IMPLICATION(canvasY >= src.y() && canvasY < src.maxY());
  left1 = -1;
  width1 = 0;
  left2 = -1;
  width2 = 0;

  if (canvasY < dst.y() || canvasY >= dst.maxY() || src.x() >= dst.maxX() ||
      src.maxX() <= dst.x()) {
    left1 = src.x();
    width1 = src.width();
    return;
  }

  if (src.x() < dst.x()) {
    left1 = src.x();
    width1 = dst.x() - src.x();
  }

  if (src.maxX() > dst.maxX()) {
    left2 = dst.maxX();
    width2 = src.maxX() - dst.maxX();
  }
}

void alphaBlendPremultiplied(blink::ImageFrame& src,
                             blink::ImageFrame& dst,
                             int canvasY,
                             int left,
                             int width) {
  for (int x = 0; x < width; ++x) {
    int canvasX = left + x;
    blink::ImageFrame::PixelData& pixel = *src.getAddr(canvasX, canvasY);
    if (SkGetPackedA32(pixel) != 0xff) {
      blink::ImageFrame::PixelData prevPixel = *dst.getAddr(canvasX, canvasY);
      pixel = SkPMSrcOver(pixel, prevPixel);
    }
  }
}

void alphaBlendNonPremultiplied(blink::ImageFrame& src,
                                blink::ImageFrame& dst,
                                int canvasY,
                                int left,
                                int width) {
  for (int x = 0; x < width; ++x) {
    int canvasX = left + x;
    blink::ImageFrame::PixelData& pixel = *src.getAddr(canvasX, canvasY);
    if (SkGetPackedA32(pixel) != 0xff) {
      blink::ImageFrame::PixelData prevPixel = *dst.getAddr(canvasX, canvasY);
      pixel = blendSrcOverDstNonPremultiplied(pixel, prevPixel);
    }
  }
}

namespace blink {

WEBPImageDecoder::WEBPImageDecoder(AlphaOption alphaOption,
                                   GammaAndColorProfileOption colorOptions,
                                   size_t maxDecodedBytes)
    : ImageDecoder(alphaOption, colorOptions, maxDecodedBytes),
      m_decoder(0),
      m_formatFlags(0),
      m_frameBackgroundHasAlpha(false),
      m_demux(0),
      m_demuxState(WEBP_DEMUX_PARSING_HEADER),
      m_haveAlreadyParsedThisData(false),
      m_repetitionCount(cAnimationLoopOnce),
      m_decodedHeight(0) {
  m_blendFunction = (alphaOption == AlphaPremultiplied)
                        ? alphaBlendPremultiplied
                        : alphaBlendNonPremultiplied;
}

WEBPImageDecoder::~WEBPImageDecoder() {
  clear();
}

void WEBPImageDecoder::clear() {
  WebPDemuxDelete(m_demux);
  m_demux = 0;
  m_consolidatedData.reset();
  clearDecoder();
}

void WEBPImageDecoder::clearDecoder() {
  WebPIDelete(m_decoder);
  m_decoder = 0;
  m_decodedHeight = 0;
  m_frameBackgroundHasAlpha = false;
}

void WEBPImageDecoder::onSetData(SegmentReader*) {
  m_haveAlreadyParsedThisData = false;
}

int WEBPImageDecoder::repetitionCount() const {
  return failed() ? cAnimationLoopOnce : m_repetitionCount;
}

bool WEBPImageDecoder::frameIsCompleteAtIndex(size_t index) const {
  if (!m_demux || m_demuxState <= WEBP_DEMUX_PARSING_HEADER)
    return false;
  if (!(m_formatFlags & ANIMATION_FLAG))
    return ImageDecoder::frameIsCompleteAtIndex(index);
  bool frameIsLoadedAtIndex = index < m_frameBufferCache.size();
  return frameIsLoadedAtIndex;
}

float WEBPImageDecoder::frameDurationAtIndex(size_t index) const {
  return index < m_frameBufferCache.size()
             ? m_frameBufferCache[index].duration()
             : 0;
}

bool WEBPImageDecoder::updateDemuxer() {
  if (failed())
    return false;

  if (m_haveAlreadyParsedThisData)
    return true;

  m_haveAlreadyParsedThisData = true;

  const unsigned webpHeaderSize = 30;
  if (m_data->size() < webpHeaderSize)
    return false;  // Await VP8X header so WebPDemuxPartial succeeds.

  WebPDemuxDelete(m_demux);
  m_consolidatedData = m_data->getAsSkData();
  WebPData inputData = {
      reinterpret_cast<const uint8_t*>(m_consolidatedData->data()),
      m_consolidatedData->size()};
  m_demux = WebPDemuxPartial(&inputData, &m_demuxState);
  if (!m_demux || (isAllDataReceived() && m_demuxState != WEBP_DEMUX_DONE)) {
    if (!m_demux)
      m_consolidatedData.reset();
    return setFailed();
  }

  ASSERT(m_demuxState > WEBP_DEMUX_PARSING_HEADER);
  if (!WebPDemuxGetI(m_demux, WEBP_FF_FRAME_COUNT))
    return false;  // Wait until the encoded image frame data arrives.

  if (!isDecodedSizeAvailable()) {
    int width = WebPDemuxGetI(m_demux, WEBP_FF_CANVAS_WIDTH);
    int height = WebPDemuxGetI(m_demux, WEBP_FF_CANVAS_HEIGHT);
    if (!setSize(width, height))
      return setFailed();

    m_formatFlags = WebPDemuxGetI(m_demux, WEBP_FF_FORMAT_FLAGS);
    if (!(m_formatFlags & ANIMATION_FLAG)) {
      m_repetitionCount = cAnimationNone;
    } else {
      // Since we have parsed at least one frame, even if partially,
      // the global animation (ANIM) properties have been read since
      // an ANIM chunk must precede the ANMF frame chunks.
      m_repetitionCount = WebPDemuxGetI(m_demux, WEBP_FF_LOOP_COUNT);
      // Repetition count is always <= 16 bits.
      ASSERT(m_repetitionCount == (m_repetitionCount & 0xffff));
      if (!m_repetitionCount)
        m_repetitionCount = cAnimationLoopInfinite;
      // FIXME: Implement ICC profile support for animated images.
      m_formatFlags &= ~ICCP_FLAG;
    }

    if ((m_formatFlags & ICCP_FLAG) && !ignoresGammaAndColorProfile())
      readColorProfile();
  }

  ASSERT(isDecodedSizeAvailable());

  size_t frameCount = WebPDemuxGetI(m_demux, WEBP_FF_FRAME_COUNT);
  updateAggressivePurging(frameCount);

  return true;
}

bool WEBPImageDecoder::initFrameBuffer(size_t frameIndex) {
  ImageFrame& buffer = m_frameBufferCache[frameIndex];
  if (buffer.getStatus() != ImageFrame::FrameEmpty)  // Already initialized.
    return true;

  const size_t requiredPreviousFrameIndex = buffer.requiredPreviousFrameIndex();
  if (requiredPreviousFrameIndex == kNotFound) {
    // This frame doesn't rely on any previous data.
    if (!buffer.setSizeAndColorProfile(size().width(), size().height(),
                                       colorProfile()))
      return setFailed();
    m_frameBackgroundHasAlpha =
        !buffer.originalFrameRect().contains(IntRect(IntPoint(), size()));
  } else {
    ImageFrame& prevBuffer = m_frameBufferCache[requiredPreviousFrameIndex];
    ASSERT(prevBuffer.getStatus() == ImageFrame::FrameComplete);

    // Preserve the last frame as the starting state for this frame. We try
    // to reuse |prevBuffer| as starting state to avoid copying.
    // For BlendAtopPreviousFrame, both frames are required, so we can't
    // take over its image data using takeBitmapDataIfWritable.
    if ((buffer.getAlphaBlendSource() == ImageFrame::BlendAtopPreviousFrame ||
         !buffer.takeBitmapDataIfWritable(&prevBuffer)) &&
        !buffer.copyBitmapData(prevBuffer))
      return setFailed();

    if (prevBuffer.getDisposalMethod() == ImageFrame::DisposeOverwriteBgcolor) {
      // We want to clear the previous frame to transparent, without
      // affecting pixels in the image outside of the frame.
      const IntRect& prevRect = prevBuffer.originalFrameRect();
      ASSERT(!prevRect.contains(IntRect(IntPoint(), size())));
      buffer.zeroFillFrameRect(prevRect);
    }

    m_frameBackgroundHasAlpha =
        prevBuffer.hasAlpha() ||
        (prevBuffer.getDisposalMethod() == ImageFrame::DisposeOverwriteBgcolor);
  }

  buffer.setStatus(ImageFrame::FramePartial);
  // The buffer is transparent outside the decoded area while the image is
  // loading. The correct alpha value for the frame will be set when it is fully
  // decoded.
  buffer.setHasAlpha(true);
  return true;
}

size_t WEBPImageDecoder::clearCacheExceptFrame(size_t clearExceptFrame) {
  // If |clearExceptFrame| has status FrameComplete, we preserve that frame.
  // Otherwise, we preserve the most recent previous frame with status
  // FrameComplete whose data will be required to decode |clearExceptFrame|,
  // either in initFrameBuffer() or ApplyPostProcessing(). All other frames can
  // be cleared.
  while ((clearExceptFrame < m_frameBufferCache.size()) &&
         (m_frameBufferCache[clearExceptFrame].getStatus() !=
          ImageFrame::FrameComplete))
    clearExceptFrame =
        m_frameBufferCache[clearExceptFrame].requiredPreviousFrameIndex();

  return ImageDecoder::clearCacheExceptFrame(clearExceptFrame);
}

void WEBPImageDecoder::clearFrameBuffer(size_t frameIndex) {
  if (m_demux && m_demuxState >= WEBP_DEMUX_PARSED_HEADER &&
      m_frameBufferCache[frameIndex].getStatus() == ImageFrame::FramePartial) {
    // Clear the decoder state so that this partial frame can be decoded again
    // when requested.
    clearDecoder();
  }
  ImageDecoder::clearFrameBuffer(frameIndex);
}

void WEBPImageDecoder::readColorProfile() {
  WebPChunkIterator chunkIterator;
  if (!WebPDemuxGetChunk(m_demux, "ICCP", 1, &chunkIterator)) {
    WebPDemuxReleaseChunkIterator(&chunkIterator);
    return;
  }

  const char* profileData =
      reinterpret_cast<const char*>(chunkIterator.chunk.bytes);
  size_t profileSize = chunkIterator.chunk.size;

  setColorProfileAndComputeTransform(profileData, profileSize,
                                     true /* hasAlpha */, false /* useSRGB */);

  WebPDemuxReleaseChunkIterator(&chunkIterator);
}

void WEBPImageDecoder::applyPostProcessing(size_t frameIndex) {
  ImageFrame& buffer = m_frameBufferCache[frameIndex];
  int width;
  int decodedHeight;
  if (!WebPIDecGetRGB(m_decoder, &decodedHeight, &width, 0, 0))
    return;  // See also https://bugs.webkit.org/show_bug.cgi?id=74062
  if (decodedHeight <= 0)
    return;

  const IntRect& frameRect = buffer.originalFrameRect();
  ASSERT_WITH_SECURITY_IMPLICATION(width == frameRect.width());
  ASSERT_WITH_SECURITY_IMPLICATION(decodedHeight <= frameRect.height());
  const int left = frameRect.x();
  const int top = frameRect.y();

#if USE(QCMSLIB)
  if (qcms_transform* transform = colorTransform()) {
    for (int y = m_decodedHeight; y < decodedHeight; ++y) {
      const int canvasY = top + y;
      uint8_t* row = reinterpret_cast<uint8_t*>(buffer.getAddr(left, canvasY));
      qcms_transform_data_type(transform, row, row, width, QCMS_OUTPUT_RGBX);
      uint8_t* pixel = row;
      for (int x = 0; x < width; ++x, pixel += 4) {
        const int canvasX = left + x;
        buffer.setRGBA(canvasX, canvasY, pixel[0], pixel[1], pixel[2],
                       pixel[3]);
      }
    }
  }
#endif  // USE(QCMSLIB)

  // During the decoding of the current frame, we may have set some pixels to be
  // transparent (i.e. alpha < 255). If the alpha blend source was
  // 'BlendAtopPreviousFrame', the values of these pixels should be determined
  // by blending them against the pixels of the corresponding previous frame.
  // Compute the correct opaque values now.
  // FIXME: This could be avoided if libwebp decoder had an API that used the
  // previous required frame to do the alpha-blending by itself.
  if ((m_formatFlags & ANIMATION_FLAG) && frameIndex &&
      buffer.getAlphaBlendSource() == ImageFrame::BlendAtopPreviousFrame &&
      buffer.requiredPreviousFrameIndex() != kNotFound) {
    ImageFrame& prevBuffer = m_frameBufferCache[frameIndex - 1];
    ASSERT(prevBuffer.getStatus() == ImageFrame::FrameComplete);
    ImageFrame::DisposalMethod prevDisposalMethod =
        prevBuffer.getDisposalMethod();
    if (prevDisposalMethod == ImageFrame::DisposeKeep) {
      // Blend transparent pixels with pixels in previous canvas.
      for (int y = m_decodedHeight; y < decodedHeight; ++y) {
        m_blendFunction(buffer, prevBuffer, top + y, left, width);
      }
    } else if (prevDisposalMethod == ImageFrame::DisposeOverwriteBgcolor) {
      const IntRect& prevRect = prevBuffer.originalFrameRect();
      // We need to blend a transparent pixel with the starting value (from just
      // after the initFrame() call). If the pixel belongs to prevRect, the
      // starting value was fully transparent, so this is a no-op. Otherwise, we
      // need to blend against the pixel from the previous canvas.
      for (int y = m_decodedHeight; y < decodedHeight; ++y) {
        int canvasY = top + y;
        int left1, width1, left2, width2;
        findBlendRangeAtRow(frameRect, prevRect, canvasY, left1, width1, left2,
                            width2);
        if (width1 > 0)
          m_blendFunction(buffer, prevBuffer, canvasY, left1, width1);
        if (width2 > 0)
          m_blendFunction(buffer, prevBuffer, canvasY, left2, width2);
      }
    }
  }

  m_decodedHeight = decodedHeight;
  buffer.setPixelsChanged(true);
}

size_t WEBPImageDecoder::decodeFrameCount() {
  // If updateDemuxer() fails, return the existing number of frames.  This way
  // if we get halfway through the image before decoding fails, we won't
  // suddenly start reporting that the image has zero frames.
  return updateDemuxer() ? WebPDemuxGetI(m_demux, WEBP_FF_FRAME_COUNT)
                         : m_frameBufferCache.size();
}

void WEBPImageDecoder::initializeNewFrame(size_t index) {
  if (!(m_formatFlags & ANIMATION_FLAG)) {
    ASSERT(!index);
    return;
  }
  WebPIterator animatedFrame;
  WebPDemuxGetFrame(m_demux, index + 1, &animatedFrame);
  ASSERT(animatedFrame.complete == 1);
  ImageFrame* buffer = &m_frameBufferCache[index];
  IntRect frameRect(animatedFrame.x_offset, animatedFrame.y_offset,
                    animatedFrame.width, animatedFrame.height);
  buffer->setOriginalFrameRect(
      intersection(frameRect, IntRect(IntPoint(), size())));
  buffer->setDuration(animatedFrame.duration);
  buffer->setDisposalMethod(animatedFrame.dispose_method ==
                                    WEBP_MUX_DISPOSE_BACKGROUND
                                ? ImageFrame::DisposeOverwriteBgcolor
                                : ImageFrame::DisposeKeep);
  buffer->setAlphaBlendSource(animatedFrame.blend_method == WEBP_MUX_BLEND
                                  ? ImageFrame::BlendAtopPreviousFrame
                                  : ImageFrame::BlendAtopBgcolor);
  buffer->setRequiredPreviousFrameIndex(
      findRequiredPreviousFrame(index, !animatedFrame.has_alpha));
  WebPDemuxReleaseIterator(&animatedFrame);
}

void WEBPImageDecoder::decode(size_t index) {
  if (failed())
    return;

  Vector<size_t> framesToDecode;
  size_t frameToDecode = index;
  do {
    framesToDecode.append(frameToDecode);
    frameToDecode =
        m_frameBufferCache[frameToDecode].requiredPreviousFrameIndex();
  } while (frameToDecode != kNotFound &&
           m_frameBufferCache[frameToDecode].getStatus() !=
               ImageFrame::FrameComplete);

  ASSERT(m_demux);
  for (auto i = framesToDecode.rbegin(); i != framesToDecode.rend(); ++i) {
    if ((m_formatFlags & ANIMATION_FLAG) && !initFrameBuffer(*i))
      return;
    WebPIterator webpFrame;
    if (!WebPDemuxGetFrame(m_demux, *i + 1, &webpFrame)) {
      setFailed();
    } else {
      decodeSingleFrame(webpFrame.fragment.bytes, webpFrame.fragment.size, *i);
      WebPDemuxReleaseIterator(&webpFrame);
    }
    if (failed())
      return;

    // We need more data to continue decoding.
    if (m_frameBufferCache[*i].getStatus() != ImageFrame::FrameComplete)
      break;

    if (m_purgeAggressively)
      clearCacheExceptFrame(*i);
  }

  // It is also a fatal error if all data is received and we have decoded all
  // frames available but the file is truncated.
  if (index >= m_frameBufferCache.size() - 1 && isAllDataReceived() &&
      m_demux && m_demuxState != WEBP_DEMUX_DONE)
    setFailed();
}

bool WEBPImageDecoder::decodeSingleFrame(const uint8_t* dataBytes,
                                         size_t dataSize,
                                         size_t frameIndex) {
  if (failed())
    return false;

  ASSERT(isDecodedSizeAvailable());

  ASSERT(m_frameBufferCache.size() > frameIndex);
  ImageFrame& buffer = m_frameBufferCache[frameIndex];
  ASSERT(buffer.getStatus() != ImageFrame::FrameComplete);

  if (buffer.getStatus() == ImageFrame::FrameEmpty) {
    if (!buffer.setSizeAndColorProfile(size().width(), size().height(),
                                       colorProfile()))
      return setFailed();
    buffer.setStatus(ImageFrame::FramePartial);
    // The buffer is transparent outside the decoded area while the image is
    // loading. The correct alpha value for the frame will be set when it is
    // fully decoded.
    buffer.setHasAlpha(true);
    buffer.setOriginalFrameRect(IntRect(IntPoint(), size()));
  }

  const IntRect& frameRect = buffer.originalFrameRect();
  if (!m_decoder) {
    WEBP_CSP_MODE mode = outputMode(m_formatFlags & ALPHA_FLAG);
    if (!m_premultiplyAlpha)
      mode = outputMode(false);
#if USE(QCMSLIB)
    if (colorTransform())
      mode = MODE_RGBA;  // Decode to RGBA for input to libqcms.
#endif
    WebPInitDecBuffer(&m_decoderBuffer);
    m_decoderBuffer.colorspace = mode;
    m_decoderBuffer.u.RGBA.stride =
        size().width() * sizeof(ImageFrame::PixelData);
    m_decoderBuffer.u.RGBA.size =
        m_decoderBuffer.u.RGBA.stride * frameRect.height();
    m_decoderBuffer.is_external_memory = 1;
    m_decoder = WebPINewDecoder(&m_decoderBuffer);
    if (!m_decoder)
      return setFailed();
  }

  m_decoderBuffer.u.RGBA.rgba =
      reinterpret_cast<uint8_t*>(buffer.getAddr(frameRect.x(), frameRect.y()));

  switch (WebPIUpdate(m_decoder, dataBytes, dataSize)) {
    case VP8_STATUS_OK:
      applyPostProcessing(frameIndex);
      buffer.setHasAlpha((m_formatFlags & ALPHA_FLAG) ||
                         m_frameBackgroundHasAlpha);
      buffer.setStatus(ImageFrame::FrameComplete);
      clearDecoder();
      return true;
    case VP8_STATUS_SUSPENDED:
      if (!isAllDataReceived() && !frameIsCompleteAtIndex(frameIndex)) {
        applyPostProcessing(frameIndex);
        return false;
      }
    // FALLTHROUGH
    default:
      clear();
      return setFailed();
  }
}

}  // namespace blink
