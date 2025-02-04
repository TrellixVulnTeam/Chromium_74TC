// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.midi;

import android.annotation.TargetApi;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.os.Build;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

@JNINamespace("midi")
/**
 * A class implementing midi::MidiDeviceAndroid functionality.
 */
@TargetApi(Build.VERSION_CODES.M)
class MidiDeviceAndroid {
    /**
     * The underlying device.
     */
    private final MidiDevice mDevice;
    /**
     * The input ports in the device.
     */
    private final MidiInputPortAndroid[] mInputPorts;
    /**
     * The output ports in the device.
     */
    private final MidiOutputPortAndroid[] mOutputPorts;
    /**
     * True when the device is open.
     */
    private boolean mIsOpen;

    /**
     * constructor
     * @param device the underlying device
     */
    MidiDeviceAndroid(MidiDevice device) {
        mIsOpen = true;
        mDevice = device;
        // Note we use "input" and "output" in the Web MIDI manner.

        mOutputPorts = new MidiOutputPortAndroid[getInfo().getInputPortCount()];
        for (int i = 0; i < mOutputPorts.length; ++i) {
            mOutputPorts[i] = new MidiOutputPortAndroid(device, i);
        }

        mInputPorts = new MidiInputPortAndroid[getInfo().getOutputPortCount()];
        for (int i = 0; i < mInputPorts.length; ++i) {
            mInputPorts[i] = new MidiInputPortAndroid(device, i);
        }
    }

    /**
     * Returns true when the device is open.
     */
    boolean isOpen() {
        return mIsOpen;
    }

    /**
     * Closes the device.
     */
    void close() {
        mIsOpen = false;
        for (MidiInputPortAndroid port : mInputPorts) {
            port.close();
        }
        for (MidiOutputPortAndroid port : mOutputPorts) {
            port.close();
        }
    }

    /**
     * Returns the underlying device.
     */
    MidiDevice getDevice() {
        return mDevice;
    }

    /**
     * Returns the underlying device information.
     */
    MidiDeviceInfo getInfo() {
        return mDevice.getInfo();
    }

    /**
     * Returns the manufacturer name.
     */
    @CalledByNative
    String getManufacturer() {
        return getProperty(MidiDeviceInfo.PROPERTY_MANUFACTURER);
    }

    /**
     * Returns the product name.
     */
    @CalledByNative
    String getProduct() {
        return getProperty(MidiDeviceInfo.PROPERTY_PRODUCT);
    }

    /**
     * Returns the version string.
     */
    @CalledByNative
    String getVersion() {
        return getProperty(MidiDeviceInfo.PROPERTY_VERSION);
    }

    /**
     * Returns the associated input ports.
     */
    @CalledByNative
    MidiInputPortAndroid[] getInputPorts() {
        return mInputPorts;
    }

    /**
     * Returns the associated output ports.
     */
    @CalledByNative
    MidiOutputPortAndroid[] getOutputPorts() {
        return mOutputPorts;
    }

    private String getProperty(String name) {
        return mDevice.getInfo().getProperties().getString(name);
    }
}
