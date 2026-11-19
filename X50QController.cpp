/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "X50QController.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
#include <sstream>
#include <thread>
#include <QDebug>

#include "X50QTargets.h"

namespace
{
constexpr unsigned int kLedSlots = 144;
constexpr unsigned int kPlaneStride = 144;
constexpr unsigned int kArrayBytes = 480;
constexpr unsigned int kModeArrayBytes = 3 * 60;
constexpr unsigned int kReportSize = 64;
constexpr unsigned int kPayloadSize = 60;
constexpr unsigned char kInterface = 2;
constexpr unsigned char kOutEndpoint = 0x03;

// Sustained color updates can make the keyboard stop responding to USB
// lighting writes. Limit traffic while the device's update tolerance is known.
constexpr auto kMinimumFrameInterval = std::chrono::milliseconds(1000);

std::string MakeStableLocation(libusb_device* device,
                               libusb_device_handle* handle,
                               std::string& serial)
{
    libusb_device_descriptor descriptor{};
    if(libusb_get_device_descriptor(device, &descriptor) == LIBUSB_SUCCESS &&
       descriptor.iSerialNumber != 0)
    {
        unsigned char serial_data[256] = {};
        const int serial_length = libusb_get_string_descriptor_ascii(
            handle, descriptor.iSerialNumber, serial_data, sizeof(serial_data));
        if(serial_length > 0)
        {
            serial.assign(reinterpret_cast<char*>(serial_data), serial_length);
        }
    }

    std::string location = "USB 24f0:202b";
    if(!serial.empty())
    {
        location += " serial=" + serial;
        return location;
    }

    unsigned char ports[8] = {};
    const int port_count = libusb_get_port_numbers(device, ports, sizeof(ports));
    if(port_count > 0)
    {
        std::ostringstream port_location;
        port_location << " port";
        for(int index = 0; index < port_count; ++index)
        {
            port_location << (index == 0 ? " " : ".") << static_cast<unsigned int>(ports[index]);
        }
        location += port_location.str();
    }
    return location;
}

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

std::array<unsigned char, kModeArrayBytes> MakeKeyEffectArray(
    const X50QController& controller)
{
    std::array<unsigned char, kModeArrayBytes> bytes{};
    if(controller.active_mode < 0 ||
       static_cast<std::size_t>(controller.active_mode) >= controller.modes.size())
    {
        return bytes;
    }

    const unsigned char selector = static_cast<unsigned char>(
        controller.modes[controller.active_mode].value);
    for(const X50QTarget& target : X50Q_TARGETS)
    {
        for(unsigned int slot : target.slot_ids)
        {
            if(slot < kLedSlots)
            {
                bytes[slot] = selector;
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
    location = MakeStableLocation(usb_device, usb_handle, serial);
    type = DEVICE_TYPE_KEYBOARD;
    version = "Unknown";

    const auto add_mode = [this](const char* name, int selector)
    {
        mode keyboard_mode;
        keyboard_mode.name = name;
        keyboard_mode.value = selector;
        keyboard_mode.flags = MODE_FLAG_HAS_PER_LED_COLOR;
        keyboard_mode.color_mode = MODE_COLORS_PER_LED;
        modes.push_back(keyboard_mode);
    };
    add_mode("Direct", 0x00);
    add_mode("Blink", 0x04);
    add_mode("Color Cycle", 0x03);
    add_mode("Breathe", 0x01);

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
    SendState(true);
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

bool X50QController::SendState(bool force)
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
    if(!force && last_color_update.time_since_epoch().count() != 0 &&
       now - last_color_update < kMinimumFrameInterval)
    {
        return true;
    }
    // Rate-limit attempts as well as successful frames. If a transfer times
    // out, the effects plugin may keep requesting updates; do not immediately
    // retry a complete report sequence on every request.
    last_color_update = now;

    unsigned char zero_payload[kPayloadSize] = {};
    // This plugin does not expose Q Software's keydown-effect selector, so
    // keep its opcode 0x0F and 0x0E parameter arrays zero-filled.
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
    const auto key_effect_array = MakeKeyEffectArray(*this);
    for(unsigned char chunk = 0; chunk < 3; ++chunk)
    {
        const unsigned char* payload = key_effect_array.data() + chunk * kPayloadSize;
        if(!SendReport(0x0D, chunk, payload))
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
