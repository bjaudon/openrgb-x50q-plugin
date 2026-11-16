/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <libusb-1.0/libusb.h>
#include <chrono>
#include <mutex>
#include <vector>

#include "RGBController.h"

class X50QController : public RGBController
{
public:
    X50QController(libusb_device* device, libusb_device_handle* handle,
                   bool detached_kernel_driver, unsigned int bus, unsigned int address);
    ~X50QController() override;

    void SetupZones() override;
    void ResizeZone(int zone, int new_size) override;
    void DeviceUpdateLEDs() override;
    void UpdateZoneLEDs(int zone) override;
    void UpdateSingleLED(int led) override;
    void DeviceUpdateMode() override;
    bool IsSameUSBDevice(libusb_device* device) const;

private:
    bool SendState();
    bool SendReport(unsigned char opcode, unsigned char chunk, const unsigned char* payload);
    bool SendArray(unsigned char opcode, const unsigned char* data, unsigned int data_size);

    libusb_device* usb_device;
    libusb_device_handle* usb_handle;
    bool detached;
    unsigned int bus_number;
    unsigned int device_address;
    std::vector<unsigned char> port_path;
    bool usb_faulted = false;
    std::chrono::steady_clock::time_point last_color_update{};
    std::mutex usb_mutex;
};
