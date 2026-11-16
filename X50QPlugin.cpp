/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "X50QPlugin.h"

#include <QDebug>
#include <QLabel>
#include <algorithm>
#include <sys/types.h>

#include "X50QController.h"

#ifndef X50Q_PLUGIN_COMMIT
#define X50Q_PLUGIN_COMMIT "unknown"
#endif

#ifndef X50Q_PLUGIN_URL
#define X50Q_PLUGIN_URL ""
#endif

OpenRGBPluginInfo X50QPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name = "DAS Keyboard X50Q";
    info.Description = "Lighting support for the DAS Keyboard X50Q";
    info.Version = "0.1.0";
    info.Commit = X50Q_PLUGIN_COMMIT;
    info.URL = X50Q_PLUGIN_URL;
    // OpenRGB API 4 expects each Devices tab to use its standard device page.
    // Keep this informational panel outside the Devices tab.
    info.Location = OPENRGB_PLUGIN_LOCATION_INFORMATION;
    info.Label = "X50Q";
    info.TabIconString = "keyboard";
    return info;
}

unsigned int X50QPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void X50QPlugin::Load(ResourceManagerInterface* resource_manager_ptr)
{
    resource_manager = resource_manager_ptr;

    libusb_context* context = nullptr;
    int result = libusb_init(&context);
    if(result != LIBUSB_SUCCESS)
    {
        qWarning() << "[X50Q] libusb_init failed:" << libusb_error_name(result);
        return;
    }
    usb_context = context;

    ScanForDevices();
    detection_timer = new QTimer(this);
    detection_timer->setInterval(5000);
    connect(detection_timer, &QTimer::timeout, this, &X50QPlugin::ScanForDevices);
    detection_timer->start();
}

void X50QPlugin::RegisterDevice(libusb_device* device)
{
    if(!resource_manager)
    {
        return;
    }

    libusb_device_descriptor descriptor{};
    if(libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS ||
       descriptor.idVendor != 0x24F0 || descriptor.idProduct != 0x202B)
    {
        return;
    }

    const bool already_registered = std::any_of(
        controllers.begin(), controllers.end(),
        [device](const X50QController* controller)
        {
            return controller->IsSameUSBDevice(device);
        });
    if(already_registered)
    {
        return;
    }

    libusb_device_handle* handle = nullptr;
    int result = libusb_open(device, &handle);
    if(result != LIBUSB_SUCCESS)
    {
        qWarning() << "[X50Q] Could not open keyboard at USB"
                   << libusb_get_bus_number(device) << ":" << libusb_get_device_address(device)
                   << libusb_error_name(result);
        return;
    }

    bool detached_driver = false;
    const int active = libusb_kernel_driver_active(handle, 2);
    if(active == 1)
    {
        result = libusb_detach_kernel_driver(handle, 2);
        detached_driver = result == LIBUSB_SUCCESS;
    }

    result = libusb_claim_interface(handle, 2);
    if(result != LIBUSB_SUCCESS)
    {
        qWarning() << "[X50Q] Could not claim interface 2 at USB"
                   << libusb_get_bus_number(device) << ":" << libusb_get_device_address(device)
                   << libusb_error_name(result);
        if(detached_driver)
        {
            libusb_attach_kernel_driver(handle, 2);
        }
        libusb_close(handle);
        return;
    }

    libusb_ref_device(device);
    X50QController* controller = new X50QController(
        device, handle, detached_driver,
        libusb_get_bus_number(device), libusb_get_device_address(device));
    controllers.push_back(controller);
    resource_manager->RegisterRGBController(controller);
    qInfo() << "[X50Q] Registered keyboard at USB"
            << controller->location.c_str();
}

void X50QPlugin::ScanForDevices()
{
    if(!usb_context || !resource_manager)
    {
        return;
    }

    libusb_device** devices = nullptr;
    const ssize_t device_count = libusb_get_device_list(usb_context, &devices);
    if(device_count < 0)
    {
        qWarning() << "[X50Q] USB enumeration failed:" << libusb_error_name(static_cast<int>(device_count));
        return;
    }

    for(auto it = controllers.begin(); it != controllers.end();)
    {
        bool present = false;
        for(ssize_t index = 0; index < device_count; ++index)
        {
            libusb_device_descriptor descriptor{};
            if(libusb_get_device_descriptor(devices[index], &descriptor) == LIBUSB_SUCCESS &&
               descriptor.idVendor == 0x24F0 && descriptor.idProduct == 0x202B &&
               (*it)->IsSameUSBDevice(devices[index]))
            {
                present = true;
                break;
            }
        }
        if(!present)
        {
            resource_manager->UnregisterRGBController(*it);
            delete *it;
            it = controllers.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for(ssize_t index = 0; index < device_count; ++index)
    {
        RegisterDevice(devices[index]);
    }

    libusb_free_device_list(devices, 1);
}

QWidget* X50QPlugin::GetWidget()
{
    QLabel* panel = new QLabel(
        "DAS Keyboard X50Q is controlled from the Devices page.\n\n"
        "Use Direct mode to set individual LEDs.");
    panel->setWordWrap(true);
    panel->setMargin(12);
    return panel;
}

QMenu* X50QPlugin::GetTrayMenu()
{
    return nullptr;
}

void X50QPlugin::Unload()
{
    if(detection_timer)
    {
        detection_timer->stop();
        delete detection_timer;
        detection_timer = nullptr;
    }
    if(resource_manager)
    {
        for(X50QController* controller : controllers)
        {
            resource_manager->UnregisterRGBController(controller);
            delete controller;
        }
    }
    controllers.clear();
    if(usb_context)
    {
        libusb_exit(usb_context);
        usb_context = nullptr;
    }
    resource_manager = nullptr;
}
