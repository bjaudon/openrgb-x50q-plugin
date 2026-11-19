# DAS Keyboard X50Q OpenRGB Controller

This plugin adds a DAS Keyboard X50Q controller to the OpenRGB build
that reports Plugin API 4. It uses the matching OpenRGB source ABI and sends
the 25-report lighting update directly to USB. It does not read or replay any
pcap files.

The key slots and effect selector values come from the controlled captures
documented in `../X50Q_PROTOCOL.md`. The plugin exposes 109 keyboard/pipe
targets plus the Q button ring as individually named LEDs in one linear zone.
The plugin provides Direct mode for static colors, plus the keyboard-side
Blink, Color Cycle, and Breathe effects. These modes set the selected effect on
all mapped LEDs while retaining per-LED color control. Keydown effects are not
exposed.

The plugin scans the USB device list at startup and every five seconds. It uses
USB bus, port path, and device address to keep an attached controller registered
without reopening its interface on each scan. Each matching keyboard is
registered as a separate controller.

Color updates are limited to 1 frame per second while validating the captured
full 25-report update sequence. Faster animation updates previously made the
keyboard stop responding to USB lighting writes. Mode changes send immediately
so the keyboard-side effect selection takes effect without waiting for the
color-update interval.

## Build against the Fedora package source

The plugin links OpenRGB's `RGBController.cpp`, so build it against the source
for the **installed OpenRGB build**, not an arbitrary current checkout. Match
the OpenRGB commit, Plugin API version, and Qt major version. Fedora RPM builds
can also include Fedora patches, so prefer the RPM's exact source RPM over an
upstream Git checkout.

### 1. Record the installed build

Query RPM for the exact installed binary package and the source RPM filename:

```bash
rpm -q --qf 'Installed package: %{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}\nSource RPM: %{SOURCERPM}\n' openrgb
dnf info --installed openrgb
```

In OpenRGB, open **About** and record the **Git Commit ID**, **Plugin API
Version**, **SDK Version**, and **Qt Version**. These identify the ABI and Qt
build the plugin needs to match. The Fedora build used for this plugin reported
commit `74cbdcce55711f57b3ca4439d3136f5bb7068db7`, Plugin API 4, and Qt 5.15.18.

If `rpm -q openrgb` says the package is not installed, this RPM procedure does
not apply. Identify the commit and Plugin API in that OpenRGB build's About
window and obtain that commit's source from the same distributor or upstream.

### 2. Download the exact RPM and source RPM

Install the RPM source tools and DNF's download command:

```bash
sudo dnf install dnf-plugins-core rpm-build rpmdevtools gcc-c++ make qt5-qtbase-devel libusb1-devel
```

Use the installed package's exact package identifier (name, version, release,
and architecture) to fetch its binary RPM, then ask DNF for that RPM's source.
Do not run an unversioned `dnf download --source openrgb`: that can select a
newer build than the one currently installed.

```bash
BUILD_ROOT="$HOME/Downloads/openrgb-x50q-build"
RPM_DIR="$BUILD_ROOT/rpms"
mkdir -p "$RPM_DIR"

OPENRGB_NEVRA=$(rpm -q --qf '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}' openrgb)
OPENRGB_SRPM=$(rpm -q --qf '%{SOURCERPM}' openrgb)
printf 'Installed NEVRA: %s\nExpected source RPM: %s\n' "$OPENRGB_NEVRA" "$OPENRGB_SRPM"

dnf download --destdir "$RPM_DIR" "$OPENRGB_NEVRA"
dnf download --source --destdir "$RPM_DIR" "$RPM_DIR/$OPENRGB_NEVRA.rpm"
test -f "$RPM_DIR/$OPENRGB_SRPM" && echo "Found matching source RPM"
```

`dnf download --source` is provided by `dnf-plugins-core`; passing the exact
local binary RPM asks it for that build's source package. [DNF download
documentation](https://dnf-plugins-core.readthedocs.io/en/latest/download.html).
If DNF cannot find this older build in the enabled repositories, use the exact
`%{SOURCERPM}` filename above to locate that build in [Fedora
Koji](https://koji.fedoraproject.org/koji/). Do not substitute the latest
OpenRGB SRPM.

### 3. Prepare the distro-patched source tree

Create the standard RPM build directories, install the source RPM into that
tree, and run only its `%prep` stage. This unpacks the source and applies the
patches listed in the spec without building or installing another OpenRGB.

```bash
rpmdev-setuptree
rpm -i "$RPM_DIR/$OPENRGB_SRPM"
rpmbuild -bp --nodeps "$HOME/rpmbuild/SPECS/openrgb.spec"
find "$HOME/rpmbuild/BUILD" -maxdepth 2 -type d -name 'OpenRGB-*' -print
```

Point `OPENRGB_SOURCE` at the prepared `OpenRGB-*` directory. The build script
uses `qmake-qt5` by default, detects the installed RPM version, reads the
OpenRGB commit from the source checkout (or its directory name), and names the
plugin with those target-build identifiers. For a source tree without Git
metadata or a commit-bearing directory name, pass `OPENRGB_COMMIT` explicitly.
For a non-RPM OpenRGB build, pass `OPENRGB_VERSION` as well.

```bash
cd /path/to/openrgb-x50q-plugin
export OPENRGB_SOURCE="$(find "$HOME/rpmbuild/BUILD" -maxdepth 2 -type d -name 'OpenRGB-*' -print -quit)"
test -f "$OPENRGB_SOURCE/RGBController/RGBController.cpp"
printf 'Using OpenRGB source: %s\n' "$OPENRGB_SOURCE"
qmake-qt5 -query QT_VERSION
./build.sh
```

For example, the Fedora `1.0~rc2` build at commit `74cbdcc` produces
`build/OpenRGBX50QPlugin_1.0rc2_Linux_amd64_74cbdcc.so`. Override detected
values when building against another OpenRGB source tree:

```bash
OPENRGB_SOURCE="$OPENRGB_SOURCE" OPENRGB_VERSION=1.0rc2 OPENRGB_COMMIT=74cbdcc ./build.sh
```

The plugin links the matching `RGBController.cpp` implementation into its
shared object. OpenRGB itself is not rebuilt. The plugin's own version is
`0.1.0`; the version and commit in the filename identify the OpenRGB build it
targets. The plugin's About URL and commit metadata are read from this
project's Git `origin` and `HEAD`. If Git metadata or an `origin` remote is not
available, the URL is blank and the plugin commit is reported as `unknown`.

## USB permissions and installation

The RPM's generated udev rules do not know about this third-party plugin's
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
- The selected keyboard-side effect is applied uniformly to all mapped LEDs.
- Keydown effects are not exposed.
- Scans run every five seconds to detect reconnects.
- The USB writes are synchronous and use the observed 2 ms report spacing.

The plugin's project URL is read from the repository's `origin` remote at build time.
