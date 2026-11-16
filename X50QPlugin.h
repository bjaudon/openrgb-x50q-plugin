/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QObject>
#include <QTimer>
#include <vector>
#include <libusb-1.0/libusb.h>

#include "OpenRGBPluginInterface.h"

class X50QController;

class X50QPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID)
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    OpenRGBPluginInfo GetPluginInfo() override;
    unsigned int GetPluginAPIVersion() override;
    void Load(ResourceManagerInterface* resource_manager_ptr) override;
    QWidget* GetWidget() override;
    QMenu* GetTrayMenu() override;
    void Unload() override;

private:
    void RegisterDevice(libusb_device* device);
    void ScanForDevices();

    ResourceManagerInterface* resource_manager = nullptr;
    std::vector<X50QController*> controllers;
    libusb_context* usb_context = nullptr;
    QTimer* detection_timer = nullptr;
};
