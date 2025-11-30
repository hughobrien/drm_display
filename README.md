

# DRM Image Display

Image viewer for embedded systems

This program displays images directly on the screen using DRM (Direct Rendering Manager) without the need for a graphical server like X11 or Wayland. Perfect for embedded systems where you need to show an image before the graphical environment starts.

## Features

- ✅ Works without X11/Wayland
- ✅ Direct access to graphics hardware via DRM
- ✅ Supports multiple image formats (JPEG, PNG, BMP, TGA, etc.)
- ✅ Automatic scaling while maintaining aspect ratio
- ✅ Automatic image centering
- ✅ Optimized for embedded systems

---

## Requirements

### System dependencies

#### Ubuntu/Debian
```sh
sudo apt-get install libdrm-dev build-essential wget
```

#### CentOS/RHEL/Fedora
```sh
sudo yum install libdrm-devel gcc make wget
```

#### Alpine Linux
```sh
sudo apk add libdrm-dev gcc make musl-dev wget
```

### Permissions

The user must have access to the DRM device:

```sh
# Add user to video group
sudo usermod -a -G video $USER

# Or run as root for testing
sudo ./drm_display image.jpg
```

## Compilation

### Standard compilation
```sh
# Install dependencies (example for Ubuntu)
make deps-debian

# Compile
make

# Install (optional)
make install

# Convert SVG images to PNG (requires rsvg-convert)
make svg2png
```

### For embedded systems
```sh
# Static compilation
make embedded

# Cross-compilation for ARM
make cross-arm
```

### Manual compilation
```sh
# Download stb_image.h
wget https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

# Compile
gcc -Wall -O2 -I/usr/include/drm -I/usr/include/libdrm drm_display.c -o drm_display -ldrm -lm
```

---

## Usage
```sh
# Basic usage
./drm_display /path/to/image.jpg

# Example with PNG image
./drm_display splash.png

# Run as root if you don't have DRM permissions
sudo ./drm_display logo.png
```

### Supported formats

- JPEG (.jpg, .jpeg)
- PNG (.png)
- BMP (.bmp)
- TGA (.tga)
- PSD (.psd)
- GIF (.gif) - only first frame
- HDR (.hdr)
- PIC (.pic)

---

## How it works

1. **Hardware detection:** The program automatically finds and configures the available DRM device (`/dev/dri/card0`).
2. **Mode selection:** Automatically chooses the highest resolution supported by your display.
3. **Image loading:** Uses the stb_image library to load various image formats.
4. **Smart scaling:** Resizes the image while maintaining aspect ratio and centers it on screen.
5. **Direct rendering:** Writes directly to the framebuffer with no intermediaries.

---

## Troubleshooting

### Error: "Cannot open /dev/dri/card0"

- Check that the device exists: `ls -la /dev/dri/`
  - Specify `DRM_CARD` to override the card number choice
- Add user to video group: `sudo usermod -a -G video $USER`
- Restart session or use sudo

### Error: "rsvg-convert: command not found" when using `make svg2png` or `make svg169`

Install the librsvg2-tools package:
```sh
sudo dnf install librsvg2-tools   # Fedora/CentOS/RHEL
sudo apt-get install librsvg2-bin # Debian/Ubuntu
```

### Error: "No connected connector found"

- Check that a display is connected
- Make sure graphics drivers are loaded: `lsmod | grep drm`

### Compilation error: "drm/drm.h: No such file"

- Install libdrm-dev: `sudo apt-get install libdrm-dev`
- On some systems: `sudo apt-get install libdrm2-dev`

### Image not displayed

- Check image file permissions
- Make sure the format is supported
- Try with sudo to rule out permission issues

---

## Usage in embedded systems

### Boot splash

To show an image during boot, you can add the program to the initramfs or call it from an early startup script:

```sh
# In /etc/init.d/ or systemd service
/usr/local/bin/drm_display /boot/splash.png &
```

### Systemd integration

Create a service to show splash at boot:

```ini
[Unit]
Description=Boot Splash
DefaultDependencies=false
After=systemd-udev-settle.service
Before=display-manager.service

[Service]
ExecStart=/usr/local/bin/drm_display /boot/splash.png
Type=forking
StandardOutput=journal

[Install]
WantedBy=multi-user.target
```

---

## Limitations

- Only supports one monitor (uses the first detected connector)
- Fixed pixel format ARGB8888
- No animation support
- Requires permissions to access DRM devices

---

## Development

The code is designed to be simple and modifiable. Main functions:

- `find_drm_device()`: Detects and configures DRM hardware
- `create_framebuffer()`: Creates the render buffer
- `display_image()`: Loads, scales, and displays the image
- `scale_and_center_image()`: Aspect-ratio-preserving scaling algorithm

To modify the behavior, edit these functions as needed.

---

## License

This project is licensed under the terms of the MIT license. See the LICENSE file for details.
