# DAS Keyboard X50Q OpenRGB Controller

This plugin adds a DAS Keyboard X50Q controller to the OpenRGB build
that reports Plugin API 4. It uses the matching OpenRGB source ABI and sends
the 25-report lighting update directly to USB. It does not read or replay any
pcap files.

The key slots and effect selector values come from the controlled captures
documented in the [DAS Keyboard X50Q lighting protocol](https://github.com/bjaudon/DasKeyboard-X50Q-Lighting-Protocol).
The plugin exposes 109 keyboard/pipe
targets plus the Q button ring as individually named LEDs in one linear zone.
It provides only Direct mode for individual LED colors. The X50Q protocol
also has captured keydown effect selectors, but those experimental modes are
omitted from this plugin because they have not been confirmed to work through
OpenRGB.

The plugin scans the USB device list at startup and every five seconds. It uses
USB bus, port path, and device address to keep an attached controller registered
without reopening its interface on each scan. Each matching keyboard is
registered as a separate controller.

Direct color updates are limited to 1 frame per second while validating the
captured full 25-report update sequence. Faster animation updates previously
made the keyboard stop responding to USB lighting writes.

## Linux build

The plugin links OpenRGB's `RGBController.cpp`. Build it against source
compatible with the OpenRGB build you use: in OpenRGB's **About** window, check
the **Git Commit ID**, **Plugin API Version**, and **Qt Version**. Choose the
matching release tag from the [OpenRGB releases](https://gitlab.com/CalcProgrammer1/OpenRGB/-/releases).
This plugin requires Plugin API 4 and Qt 5.

This plugin has been built and tested on Fedora 45. It should work on other
Linux distributions with equivalent Qt 5, libusb 1.0, compiler, and build
tools. The instructions use Fedora as the primary example; Debian/Ubuntu
package names are included where they differ.

Install the build tools and Qt/libusb development files. For example:

```bash
# Fedora
sudo dnf install gcc-c++ make qt5-qtbase-devel libusb1-devel curl tar

# Debian/Ubuntu
sudo apt install build-essential qtbase5-dev qt5-qmake libusb-1.0-0-dev curl
```

From the plugin repository's root, pass the matching GitLab release tag:

```bash
OPENRGB_TAG=release_candidate_1.0rc2 ./build.sh
```

The script downloads and caches the release source archive under `build/`,
extracts it, and builds the plugin; it does not install or rebuild OpenRGB.
Release tags such as `release_1.0` and `release_candidate_1.0rc2` determine the
OpenRGB version in the output filename. The script cleans old object files
before building, so changing tags cannot reuse objects from another source
tree.

For distro-patched OpenRGB builds, or if you already have the exact compatible
source tree, pass it directly instead of downloading a release archive:

```bash
OPENRGB_SOURCE="/absolute/path/to/OpenRGB-source" OPENRGB_VERSION=1.0rc2 ./build.sh
```

The plugin links the selected `RGBController.cpp` into its shared object.
The plugin's own version is `0.1.0`; the filename's version identifies the
OpenRGB version, and its commit suffix identifies this plugin repository's Git
`HEAD`. The plugin's About URL and commit metadata are also read from this
project's Git `origin` and `HEAD`. If Git metadata or an `origin` remote is not
available, the URL is blank and the plugin commit is reported as `unknown`.

## USB permissions and installation

Some OpenRGB packages' generated udev rules may not include this plugin's
VID/PID. Install the included rule so the desktop session can open the USB
device without running OpenRGB as root:

```bash
sudo install -m 0644 60-openrgb-x50q.rules /etc/udev/rules.d/60-openrgb-x50q.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb
```

Unplug and reconnect the keyboard after reloading the rule. Install the plugin
for the current user and restart OpenRGB:

```bash
mkdir -p ~/.config/OpenRGB/plugins
cp build/OpenRGBX50QPlugin_*.so ~/.config/OpenRGB/plugins/
```

On startup, check OpenRGB's output for `[X50Q] Registered keyboard`. When
changing a color, `[X50Q] Applying state` confirms the OpenRGB update reached
the USB writer. If the
keyboard is found but interface 2 cannot be claimed, close other lighting
software and check that the udev rule applied to the USB device.

## Current limits
- The displayed LEDs are in a named linear list rather than a keyboard-shaped
  matrix. Their names match the captured key labels, including `leftpipe`,
  `rightpipe`, and `Q button ring`.
- Keydown effects are not exposed.
- Scans run every five seconds to detect reconnects.
- The USB writes are synchronous and use the observed 2 ms report spacing.

The plugin's project URL is read from the repository's `origin` remote at build time.
