// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.media;

import android.annotation.TargetApi;
import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaCrypto;
import android.media.MediaFormat;
import android.os.Build;
import android.os.Bundle;
import android.view.Surface;

import org.chromium.base.Log;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;
import org.chromium.media.MediaCodecUtil.MimeTypes;

import java.nio.ByteBuffer;

/**
 * A wrapper of the MediaCodec class to facilitate exception capturing and
 * audio rendering.
 */
@JNINamespace("media")
class MediaCodecBridge {
    private static final String TAG = "cr_media";

    // Error code for MediaCodecBridge. Keep this value in sync with
    // MediaCodecStatus in media_codec_bridge.h.
    private static final int MEDIA_CODEC_OK = 0;
    private static final int MEDIA_CODEC_DEQUEUE_INPUT_AGAIN_LATER = 1;
    private static final int MEDIA_CODEC_DEQUEUE_OUTPUT_AGAIN_LATER = 2;
    private static final int MEDIA_CODEC_OUTPUT_BUFFERS_CHANGED = 3;
    private static final int MEDIA_CODEC_OUTPUT_FORMAT_CHANGED = 4;
    private static final int MEDIA_CODEC_INPUT_END_OF_STREAM = 5;
    private static final int MEDIA_CODEC_OUTPUT_END_OF_STREAM = 6;
    private static final int MEDIA_CODEC_NO_KEY = 7;
    private static final int MEDIA_CODEC_ABORT = 8;
    private static final int MEDIA_CODEC_ERROR = 9;

    // After a flush(), dequeueOutputBuffer() can often produce empty presentation timestamps
    // for several frames. As a result, the player may find that the time does not increase
    // after decoding a frame. To detect this, we check whether the presentation timestamp from
    // dequeueOutputBuffer() is larger than input_timestamp - MAX_PRESENTATION_TIMESTAMP_SHIFT_US
    // after a flush. And we set the presentation timestamp from dequeueOutputBuffer() to be
    // non-decreasing for the remaining frames.
    private static final long MAX_PRESENTATION_TIMESTAMP_SHIFT_US = 100000;

    // We use only one output audio format (PCM16) that has 2 bytes per sample
    private static final int PCM16_BYTES_PER_SAMPLE = 2;

    // TODO(qinmin): Use MediaFormat constants when part of the public API.
    private static final String KEY_CROP_LEFT = "crop-left";
    private static final String KEY_CROP_RIGHT = "crop-right";
    private static final String KEY_CROP_BOTTOM = "crop-bottom";
    private static final String KEY_CROP_TOP = "crop-top";

    private ByteBuffer[] mInputBuffers;
    private ByteBuffer[] mOutputBuffers;

    private MediaCodec mMediaCodec;
    private boolean mFlushed;
    private long mLastPresentationTimeUs;
    private String mMime;
    private boolean mAdaptivePlaybackSupported;

    @MainDex
    private static class DequeueInputResult {
        private final int mStatus;
        private final int mIndex;

        private DequeueInputResult(int status, int index) {
            mStatus = status;
            mIndex = index;
        }

        @CalledByNative("DequeueInputResult")
        private int status() {
            return mStatus;
        }

        @CalledByNative("DequeueInputResult")
        private int index() {
            return mIndex;
        }
    }

    @MainDex
    private static class DequeueOutputResult {
        private final int mStatus;
        private final int mIndex;
        private final int mFlags;
        private final int mOffset;
        private final long mPresentationTimeMicroseconds;
        private final int mNumBytes;

        private DequeueOutputResult(int status, int index, int flags, int offset,
                long presentationTimeMicroseconds, int numBytes) {
            mStatus = status;
            mIndex = index;
            mFlags = flags;
            mOffset = offset;
            mPresentationTimeMicroseconds = presentationTimeMicroseconds;
            mNumBytes = numBytes;
        }

        @CalledByNative("DequeueOutputResult")
        private int status() {
            return mStatus;
        }

        @CalledByNative("DequeueOutputResult")
        private int index() {
            return mIndex;
        }

        @CalledByNative("DequeueOutputResult")
        private int flags() {
            return mFlags;
        }

        @CalledByNative("DequeueOutputResult")
        private int offset() {
            return mOffset;
        }

        @CalledByNative("DequeueOutputResult")
        private long presentationTimeMicroseconds() {
            return mPresentationTimeMicroseconds;
        }

        @CalledByNative("DequeueOutputResult")
        private int numBytes() {
            return mNumBytes;
        }
    }

    /** A wrapper around a MediaFormat. */
    @MainDex
    private static class GetOutputFormatResult {
        private final int mStatus;
        // May be null if mStatus is not MEDIA_CODEC_OK.
        private final MediaFormat mFormat;

        private GetOutputFormatResult(int status, MediaFormat format) {
            mStatus = status;
            mFormat = format;
        }

        private boolean formatHasCropValues() {
            return mFormat.containsKey(KEY_CROP_RIGHT) && mFormat.containsKey(KEY_CROP_LEFT)
                    && mFormat.containsKey(KEY_CROP_BOTTOM) && mFormat.containsKey(KEY_CROP_TOP);
        }

        @CalledByNative("GetOutputFormatResult")
        private int status() {
            return mStatus;
        }

        @CalledByNative("GetOutputFormatResult")
        private int width() {
            return formatHasCropValues()
                    ? mFormat.getInteger(KEY_CROP_RIGHT) - mFormat.getInteger(KEY_CROP_LEFT) + 1
                    : mFormat.getInteger(MediaFormat.KEY_WIDTH);
        }

        @CalledByNative("GetOutputFormatResult")
        private int height() {
            return formatHasCropValues()
                    ? mFormat.getInteger(KEY_CROP_BOTTOM) - mFormat.getInteger(KEY_CROP_TOP) + 1
                    : mFormat.getInteger(MediaFormat.KEY_HEIGHT);
        }

        @CalledByNative("GetOutputFormatResult")
        private int sampleRate() {
            return mFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE);
        }

        @CalledByNative("GetOutputFormatResult")
        private int channelCount() {
            return mFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
        }
    }

    private MediaCodecBridge(
            MediaCodec mediaCodec, String mime, boolean adaptivePlaybackSupported) {
        assert mediaCodec != null;
        mMediaCodec = mediaCodec;
        mMime = mime;
        mLastPresentationTimeUs = 0;
        mFlushed = true;
        mAdaptivePlaybackSupported = adaptivePlaybackSupported;
    }

    @CalledByNative
    private static MediaCodecBridge create(
            String mime, boolean isSecure, int direction, boolean requireSoftwareCodec) {
        MediaCodecUtil.CodecCreationInfo info = new MediaCodecUtil.CodecCreationInfo();
        try {
            if (direction == MediaCodecUtil.MEDIA_CODEC_ENCODER) {
                info.mediaCodec = MediaCodec.createEncoderByType(mime);
                info.supportsAdaptivePlayback = false;
            } else {
                // |isSecure| only applies to video decoders.
                info = MediaCodecUtil.createDecoder(mime, isSecure, requireSoftwareCodec);
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to create MediaCodec: %s, isSecure: %s, direction: %d",
                    mime, isSecure, direction, e);
        }

        if (info.mediaCodec == null) return null;

        return new MediaCodecBridge(info.mediaCodec, mime, info.supportsAdaptivePlayback);
    }

    @CalledByNative
    private void release() {
        try {
            String codecName = "unknown";
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR2) {
                codecName = mMediaCodec.getName();
            }
            Log.w(TAG, "calling MediaCodec.release() on " + codecName);
            mMediaCodec.release();
        } catch (IllegalStateException e) {
            // The MediaCodec is stuck in a wrong state, possibly due to losing
            // the surface.
            Log.e(TAG, "Cannot release media codec", e);
        }
        mMediaCodec = null;
    }

    @SuppressWarnings("deprecation")
    @CalledByNative
    private boolean start() {
        try {
            mMediaCodec.start();
            if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.KITKAT) {
                mInputBuffers = mMediaCodec.getInputBuffers();
                mOutputBuffers = mMediaCodec.getOutputBuffers();
            }
        } catch (IllegalStateException e) {
            Log.e(TAG, "Cannot start the media codec", e);
            return false;
        } catch (IllegalArgumentException e) {
            Log.e(TAG, "Cannot start the media codec", e);
            return false;
        }
        return true;
    }

    @CalledByNative
    private DequeueInputResult dequeueInputBuffer(long timeoutUs) {
        int status = MEDIA_CODEC_ERROR;
        int index = -1;
        try {
            int indexOrStatus = mMediaCodec.dequeueInputBuffer(timeoutUs);
            if (indexOrStatus >= 0) { // index!
                status = MEDIA_CODEC_OK;
                index = indexOrStatus;
            } else if (indexOrStatus == MediaCodec.INFO_TRY_AGAIN_LATER) {
                status = MEDIA_CODEC_DEQUEUE_INPUT_AGAIN_LATER;
            } else {
                Log.e(TAG, "Unexpected index_or_status: " + indexOrStatus);
                assert false;
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to dequeue input buffer", e);
        }
        return new DequeueInputResult(status, index);
    }

    @CalledByNative
    private int flush() {
        try {
            mFlushed = true;
            mMediaCodec.flush();
        } catch (IllegalStateException e) {
            Log.e(TAG, "Failed to flush MediaCodec", e);
            return MEDIA_CODEC_ERROR;
        }
        return MEDIA_CODEC_OK;
    }

    @CalledByNative
    private void stop() {
        mMediaCodec.stop();
    }

    @TargetApi(Build.VERSION_CODES.KITKAT)
    @CalledByNative
    private String getName() {
        return mMediaCodec.getName();
    }

    @CalledByNative
    private GetOutputFormatResult getOutputFormat() {
        MediaFormat format = null;
        int status = MEDIA_CODEC_OK;
        try {
            format = mMediaCodec.getOutputFormat();
        } catch (IllegalStateException e) {
            Log.e(TAG, "Failed to get output format", e);
            status = MEDIA_CODEC_ERROR;
        }
        return new GetOutputFormatResult(status, format);
    }

    /** Returns null if MediaCodec throws IllegalStateException. */
    @CalledByNative
    private ByteBuffer getInputBuffer(int index) {
        if (Build.VERSION.SDK_INT > Build.VERSION_CODES.KITKAT) {
            try {
                return mMediaCodec.getInputBuffer(index);
            } catch (IllegalStateException e) {
                Log.e(TAG, "Failed to get input buffer", e);
                return null;
            }
        }
        return mInputBuffers[index];
    }

    /** Returns null if MediaCodec throws IllegalStateException. */
    @CalledByNative
    private ByteBuffer getOutputBuffer(int index) {
        if (Build.VERSION.SDK_INT > Build.VERSION_CODES.KITKAT) {
            try {
                return mMediaCodec.getOutputBuffer(index);
            } catch (IllegalStateException e) {
                Log.e(TAG, "Failed to get output buffer", e);
                return null;
            }
        }
        return mOutputBuffers[index];
    }

    @CalledByNative
    private int queueInputBuffer(
            int index, int offset, int size, long presentationTimeUs, int flags) {
        resetLastPresentationTimeIfNeeded(presentationTimeUs);
        try {
            mMediaCodec.queueInputBuffer(index, offset, size, presentationTimeUs, flags);
        } catch (Exception e) {
            Log.e(TAG, "Failed to queue input buffer", e);
            return MEDIA_CODEC_ERROR;
        }
        return MEDIA_CODEC_OK;
    }

    @TargetApi(Build.VERSION_CODES.KITKAT)
    @CalledByNative
    private void setVideoBitrate(int bps) {
        Bundle b = new Bundle();
        b.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, bps);
        mMediaCodec.setParameters(b);
    }

    @TargetApi(Build.VERSION_CODES.KITKAT)
    @CalledByNative
    private void requestKeyFrameSoon() {
        Bundle b = new Bundle();
        b.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
        mMediaCodec.setParameters(b);
    }

    @CalledByNative
    private int queueSecureInputBuffer(
            int index, int offset, byte[] iv, byte[] keyId, int[] numBytesOfClearData,
            int[] numBytesOfEncryptedData, int numSubSamples, long presentationTimeUs) {
        resetLastPresentationTimeIfNeeded(presentationTimeUs);
        try {
            MediaCodec.CryptoInfo cryptoInfo = new MediaCodec.CryptoInfo();
            cryptoInfo.set(numSubSamples, numBytesOfClearData, numBytesOfEncryptedData,
                    keyId, iv, MediaCodec.CRYPTO_MODE_AES_CTR);
            mMediaCodec.queueSecureInputBuffer(index, offset, cryptoInfo, presentationTimeUs, 0);
        } catch (MediaCodec.CryptoException e) {
            if (e.getErrorCode() == MediaCodec.CryptoException.ERROR_NO_KEY) {
                Log.d(TAG, "Failed to queue secure input buffer: CryptoException.ERROR_NO_KEY");
                return MEDIA_CODEC_NO_KEY;
            }
            Log.e(TAG, "Failed to queue secure input buffer, CryptoException with error code "
                            + e.getErrorCode());
            return MEDIA_CODEC_ERROR;
        } catch (IllegalStateException e) {
            Log.e(TAG, "Failed to queue secure input buffer, IllegalStateException " + e);
            return MEDIA_CODEC_ERROR;
        }
        return MEDIA_CODEC_OK;
    }

    @CalledByNative
    private void releaseOutputBuffer(int index, boolean render) {
        try {
            mMediaCodec.releaseOutputBuffer(index, render);
        } catch (IllegalStateException e) {
            // TODO(qinmin): May need to report the error to the caller. crbug.com/356498.
            Log.e(TAG, "Failed to release output buffer", e);
        }
    }

    @SuppressWarnings("deprecation")
    @CalledByNative
    private DequeueOutputResult dequeueOutputBuffer(long timeoutUs) {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        int status = MEDIA_CODEC_ERROR;
        int index = -1;
        try {
            int indexOrStatus = mMediaCodec.dequeueOutputBuffer(info, timeoutUs);
            if (info.presentationTimeUs < mLastPresentationTimeUs) {
                // TODO(qinmin): return a special code through DequeueOutputResult
                // to notify the native code the the frame has a wrong presentation
                // timestamp and should be skipped.
                info.presentationTimeUs = mLastPresentationTimeUs;
            }
            mLastPresentationTimeUs = info.presentationTimeUs;

            if (indexOrStatus >= 0) { // index!
                status = MEDIA_CODEC_OK;
                index = indexOrStatus;
            } else if (indexOrStatus == MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED) {
                assert Build.VERSION.SDK_INT <= Build.VERSION_CODES.KITKAT;
                mOutputBuffers = mMediaCodec.getOutputBuffers();
                status = MEDIA_CODEC_OUTPUT_BUFFERS_CHANGED;
            } else if (indexOrStatus == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                status = MEDIA_CODEC_OUTPUT_FORMAT_CHANGED;
                MediaFormat newFormat = mMediaCodec.getOutputFormat();
            } else if (indexOrStatus == MediaCodec.INFO_TRY_AGAIN_LATER) {
                status = MEDIA_CODEC_DEQUEUE_OUTPUT_AGAIN_LATER;
            } else {
                Log.e(TAG, "Unexpected index_or_status: " + indexOrStatus);
                assert false;
            }
        } catch (IllegalStateException e) {
            status = MEDIA_CODEC_ERROR;
            Log.e(TAG, "Failed to dequeue output buffer", e);
        }

        return new DequeueOutputResult(
                status, index, info.flags, info.offset, info.presentationTimeUs, info.size);
    }

    @CalledByNative
    private boolean configureVideo(MediaFormat format, Surface surface, MediaCrypto crypto,
            int flags, boolean allowAdaptivePlayback) {
        try {
            // If adaptive playback is turned off by request, then treat it as
            // not supported.  Note that configureVideo is only called once
            // during creation, else this would prevent re-enabling adaptive
            // playback later.
            if (!allowAdaptivePlayback) mAdaptivePlaybackSupported = false;

            if (mAdaptivePlaybackSupported) {
                // The max size is a hint to the codec, and causes it to
                // allocate more memory up front.  It still supports higher
                // resolutions if they arrive.  So, we try to ask only for
                // the initial size.
                format.setInteger(
                        MediaFormat.KEY_MAX_WIDTH, format.getInteger(MediaFormat.KEY_WIDTH));
                format.setInteger(
                        MediaFormat.KEY_MAX_HEIGHT, format.getInteger(MediaFormat.KEY_HEIGHT));
            }
            maybeSetMaxInputSize(format);
            mMediaCodec.configure(format, surface, crypto, flags);
            return true;
        } catch (IllegalArgumentException e) {
            Log.e(TAG, "Cannot configure the video codec, wrong format or surface", e);
        } catch (IllegalStateException e) {
            Log.e(TAG, "Cannot configure the video codec", e);
        } catch (MediaCodec.CryptoException e) {
            Log.e(TAG, "Cannot configure the video codec: DRM error", e);
        } catch (Exception e) {
            Log.e(TAG, "Cannot configure the video codec", e);
        }
        return false;
    }

    @CalledByNative
    private static MediaFormat createAudioFormat(String mime, int sampleRate, int channelCount) {
        return MediaFormat.createAudioFormat(mime, sampleRate, channelCount);
    }

    @CalledByNative
    private static MediaFormat createVideoDecoderFormat(String mime, int width, int height) {
        return MediaFormat.createVideoFormat(mime, width, height);
    }

    // Use some heuristics to set KEY_MAX_INPUT_SIZE (the size of the input buffers).
    // Taken from exoplayer:
    // https://github.com/google/ExoPlayer/blob/8595c65678a181296cdf673eacb93d8135479340/library/src/main/java/com/google/android/exoplayer/MediaCodecVideoTrackRenderer.java
    private void maybeSetMaxInputSize(MediaFormat format) {
        if (format.containsKey(android.media.MediaFormat.KEY_MAX_INPUT_SIZE)) {
            // Already set. The source of the format may know better, so do nothing.
            return;
        }
        int maxHeight = format.getInteger(MediaFormat.KEY_HEIGHT);
        if (mAdaptivePlaybackSupported && format.containsKey(MediaFormat.KEY_MAX_HEIGHT)) {
            maxHeight = Math.max(maxHeight, format.getInteger(MediaFormat.KEY_MAX_HEIGHT));
        }
        int maxWidth = format.getInteger(MediaFormat.KEY_WIDTH);
        if (mAdaptivePlaybackSupported && format.containsKey(MediaFormat.KEY_MAX_WIDTH)) {
            maxWidth = Math.max(maxHeight, format.getInteger(MediaFormat.KEY_MAX_WIDTH));
        }
        int maxPixels;
        int minCompressionRatio;
        switch (format.getString(MediaFormat.KEY_MIME)) {
            case MimeTypes.VIDEO_H264:
                if ("BRAVIA 4K 2015".equals(Build.MODEL)) {
                    // The Sony BRAVIA 4k TV has input buffers that are too small for the calculated
                    // 4k video maximum input size, so use the default value.
                    return;
                }
                // Round up width/height to an integer number of macroblocks.
                maxPixels = ((maxWidth + 15) / 16) * ((maxHeight + 15) / 16) * 16 * 16;
                minCompressionRatio = 2;
                break;
            case MimeTypes.VIDEO_VP8:
                // VPX does not specify a ratio so use the values from the platform's SoftVPX.cpp.
                maxPixels = maxWidth * maxHeight;
                minCompressionRatio = 2;
                break;
            case MimeTypes.VIDEO_H265:
            case MimeTypes.VIDEO_VP9:
                maxPixels = maxWidth * maxHeight;
                minCompressionRatio = 4;
                break;
            default:
                // Leave the default max input size.
                return;
        }
        // Estimate the maximum input size assuming three channel 4:2:0 subsampled input frames.
        int maxInputSize = (maxPixels * 3) / (2 * minCompressionRatio);
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, maxInputSize);
    }

    @CalledByNative
    private static MediaFormat createVideoEncoderFormat(String mime, int width, int height,
            int bitRate, int frameRate, int iFrameInterval, int colorFormat) {
        MediaFormat format = MediaFormat.createVideoFormat(mime, width, height);
        format.setInteger(MediaFormat.KEY_BIT_RATE, bitRate);
        format.setInteger(MediaFormat.KEY_FRAME_RATE, frameRate);
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, iFrameInterval);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT, colorFormat);
        return format;
    }

    @CalledByNative
    private boolean isAdaptivePlaybackSupported(int width, int height) {
        // If media codec has adaptive playback supported, then the max sizes
        // used during creation are only hints.
        return mAdaptivePlaybackSupported;
    }

    @CalledByNative
    private static void setCodecSpecificData(MediaFormat format, int index, byte[] bytes) {
        // Codec Specific Data is set in the MediaFormat as ByteBuffer entries with keys csd-0,
        // csd-1, and so on. See: http://developer.android.com/reference/android/media/MediaCodec.html
        // for details.
        String name;
        switch (index) {
            case 0:
                name = "csd-0";
                break;
            case 1:
                name = "csd-1";
                break;
            case 2:
                name = "csd-2";
                break;
            default:
                name = null;
                break;
        }
        if (name != null) {
            format.setByteBuffer(name, ByteBuffer.wrap(bytes));
        }
    }

    @CalledByNative
    private static void setFrameHasADTSHeader(MediaFormat format) {
        format.setInteger(MediaFormat.KEY_IS_ADTS, 1);
    }

    @CalledByNative
    private boolean configureAudio(MediaFormat format, MediaCrypto crypto, int flags) {
        try {
            mMediaCodec.configure(format, null, crypto, flags);
            return true;
        } catch (IllegalArgumentException e) {
            Log.e(TAG, "Cannot configure the audio codec", e);
        } catch (IllegalStateException e) {
            Log.e(TAG, "Cannot configure the audio codec", e);
        } catch (MediaCodec.CryptoException e) {
            Log.e(TAG, "Cannot configure the audio codec: DRM error", e);
        } catch (Exception e) {
            Log.e(TAG, "Cannot configure the audio codec", e);
        }
        return false;
    }

    private void resetLastPresentationTimeIfNeeded(long presentationTimeUs) {
        if (mFlushed) {
            mLastPresentationTimeUs =
                    Math.max(presentationTimeUs - MAX_PRESENTATION_TIMESTAMP_SHIFT_US, 0);
            mFlushed = false;
        }
    }

    @SuppressWarnings("deprecation")
    private int getAudioFormat(int channelCount) {
        switch (channelCount) {
            case 1:
                return AudioFormat.CHANNEL_OUT_MONO;
            case 2:
                return AudioFormat.CHANNEL_OUT_STEREO;
            case 4:
                return AudioFormat.CHANNEL_OUT_QUAD;
            case 6:
                return AudioFormat.CHANNEL_OUT_5POINT1;
            case 8:
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    return AudioFormat.CHANNEL_OUT_7POINT1_SURROUND;
                } else {
                    return AudioFormat.CHANNEL_OUT_7POINT1;
                }
            default:
                return AudioFormat.CHANNEL_OUT_DEFAULT;
        }
    }
}
