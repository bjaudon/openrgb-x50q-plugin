/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "X50QController.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
#include <thread>
#include <QDebug>

#include "X50QTargets.h"

namespace
{
constexpr unsigned int kLedSlots = 144;
constexpr unsigned int kPlaneStride = 144;
constexpr unsigned int kArrayBytes = 480;
constexpr unsigned int kReportSize = 64;
constexpr unsigned int kPayloadSize = 60;
constexpr unsigned char kInterface = 2;
constexpr unsigned char kOutEndpoint = 0x03;

// Sustained direct updates can make the keyboard stop responding to USB
// lighting writes. Limit traffic while the device's update tolerance is known.
constexpr auto kMinimumFrameInterval = std::chrono::milliseconds(1000);

std::array<unsigned char, kArrayBytes> MakeColorArray(const X50QController& controller)
{
    std::array<unsigned char, kArrayBytes> bytes{};
    for(std::size_t led_index = 0; led_index < X50Q_TARGETS.size(); ++led_index)
    {
        const RGBColor color = controller.colors[led_index];
        const unsigned char channels[] = {
            static_cast<unsigned char>(RGBGetRValue(color)),
            static_cast<unsigned char>(RGBGetGValue(color)),
            static_cast<unsigned char>(RGBGetBValue(color)),
        };
        for(unsigned int slot : X50Q_TARGETS[led_index].slot_ids)
        {
            if(slot >= kLedSlots)
            {
                continue;
            }
            for(unsigned int channel = 0; channel < 3; ++channel)
            {
                bytes[slot + channel * kPlaneStride] = channels[channel];
            }
        }
    }
    return bytes;
}
}

X50QController::X50QController(libusb_device* device, libusb_device_handle* handle,
                               bool detached_kernel_driver,
                               unsigned int bus, unsigned int address)
    : usb_device(device), usb_handle(handle),
      detached(detached_kernel_driver), bus_number(bus), device_address(address)
{
    unsigned char ports[8] = {};
    const int port_count = libusb_get_port_numbers(device, ports, sizeof(ports));
    if(port_count > 0)
    {
        port_path.assign(ports, ports + port_count);
    }

    name = "DAS Keyboard X50Q";
    vendor = "Das Keyboard";
    description = "X50 RGB Mechanical Keyboard";
    location = "USB " + std::to_string(bus_number) + ":" + std::to_string(device_address);
    type = DEVICE_TYPE_KEYBOARD;
    version = "Unknown";
    serial = "";

    mode direct_mode;
    direct_mode.name = "Direct";
    direct_mode.value = 0;
    direct_mode.flags = MODE_FLAG_HAS_PER_LED_COLOR;
    direct_mode.color_mode = MODE_COLORS_PER_LED;
    modes.push_back(direct_mode);

    SetupZones();
}

X50QController::~X50QController()
{
    std::lock_guard<std::mutex> lock(usb_mutex);
    if(usb_handle)
    {
        libusb_release_interface(usb_handle, kInterface);
        if(detached)
        {
            libusb_attach_kernel_driver(usb_handle, kInterface);
        }
        libusb_close(usb_handle);
        usb_handle = nullptr;
    }
    if(usb_device)
    {
        libusb_unref_device(usb_device);
        usb_device = nullptr;
    }
}

bool X50QController::IsSameUSBDevice(libusb_device* device) const
{
    if(libusb_get_bus_number(device) != bus_number)
    {
        return false;
    }

    unsigned char ports[8] = {};
    const int port_count = libusb_get_port_numbers(device, ports, sizeof(ports));
    if(port_count <= 0 || port_path.empty())
    {
        return usb_device == device;
    }

    return libusb_get_device_address(device) == device_address &&
           static_cast<std::size_t>(port_count) == port_path.size() &&
           std::equal(port_path.begin(), port_path.end(), ports);
}

void X50QController::SetupZones()
{
    zone keyboard_zone;
    keyboard_zone.name = "Keyboard and auxiliary LEDs";
    keyboard_zone.type = ZONE_TYPE_LINEAR;
    keyboard_zone.leds_min = static_cast<unsigned int>(X50Q_TARGETS.size());
    keyboard_zone.leds_max = static_cast<unsigned int>(X50Q_TARGETS.size());
    keyboard_zone.leds_count = static_cast<unsigned int>(X50Q_TARGETS.size());
    keyboard_zone.matrix_map = nullptr;
    zones.push_back(keyboard_zone);

    for(const X50QTarget& target : X50Q_TARGETS)
    {
        led keyboard_led;
        keyboard_led.name = target.name;
        leds.push_back(keyboard_led);
    }

    SetupColors();
}

void X50QController::ResizeZone(int, int)
{
}

void X50QController::DeviceUpdateLEDs()
{
    SendState();
}

void X50QController::UpdateZoneLEDs(int)
{
    SendState();
}

void X50QController::UpdateSingleLED(int)
{
    SendState();
}

void X50QController::DeviceUpdateMode()
{
    SendState();
}

bool X50QController::SendReport(unsigned char opcode, unsigned char chunk,
                                const unsigned char* payload)
{
    unsigned char report[kReportSize] = {0x07, opcode, 0x06, chunk};
    std::copy(payload, payload + kPayloadSize, report + 4);

    int transferred = 0;
    const int result = libusb_interrupt_transfer(
        usb_handle, kOutEndpoint, report, kReportSize, &transferred, 1000);
    if(result != LIBUSB_SUCCESS || transferred != kReportSize)
    {
        qWarning("[X50Q] USB write failed at %u:%u (opcode 0x%02x chunk %u): %s, %d/%u bytes",
                 bus_number, device_address, opcode, chunk,
                 libusb_error_name(result), transferred, kReportSize);
        usb_faulted = true;
        qWarning("[X50Q] Pausing further USB updates; unplug and reconnect the keyboard to recover");
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return true;
}

bool X50QController::SendArray(unsigned char opcode, const unsigned char* data,
                               unsigned int data_size)
{
    if(data_size != kArrayBytes)
    {
        return false;
    }
    for(unsigned char chunk = 0; chunk < 8; ++chunk)
    {
        if(!SendReport(opcode, chunk, data + chunk * kPayloadSize))
        {
            return false;
        }
    }
    return true;
}

bool X50QController::SendState()
{
    std::lock_guard<std::mutex> lock(usb_mutex);
    if(!usb_handle || modes.empty())
    {
        qWarning() << "[X50Q] Update skipped: controller is not ready";
        return false;
    }
    if(usb_faulted)
    {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if(last_color_update.time_since_epoch().count() != 0 &&
       now - last_color_update < kMinimumFrameInterval)
    {
        return true;
    }
    // Rate-limit attempts as well as successful frames. If a transfer times
    // out, the effects plugin may keep requesting updates; do not immediately
    // retry a complete report sequence on every request.
    last_color_update = now;

    unsigned char zero_payload[kPayloadSize] = {};
    // Captured Q Software direct-mode frames send zero-filled parameter arrays
    // for opcodes 0x0F and 0x0E. The firmware treats 0x0F as per-key state.
    for(unsigned char chunk = 0; chunk < 3; ++chunk)
    {
        if(!SendReport(0x0F, chunk, zero_payload))
        {
            return false;
        }
    }
    for(unsigned char chunk = 0; chunk < 3; ++chunk)
    {
        if(!SendReport(0x0E, chunk, zero_payload))
        {
            return false;
        }
    }

    const auto color_array = MakeColorArray(*this);
    if(!SendArray(0x0A, color_array.data(), color_array.size()))
    {
        return false;
    }
    for(unsigned char chunk = 0; chunk < 3; ++chunk)
    {
        if(!SendReport(0x0D, chunk, zero_payload))
        {
            return false;
        }
    }

    if(!SendArray(0x09, color_array.data(), color_array.size()))
    {
        return false;
    }
    return true;
}
