#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>


// For compatibility with older systems
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

struct drm_device {
    int fd;
    drmModeRes *resources;
    drmModeConnector *connector;
    drmModeEncoder *encoder;
    drmModeCrtc *crtc;
    drmModeModeInfo mode;
    uint32_t fb_id;
    void *map;
    size_t size;
    uint32_t handle;
    uint32_t pitch;
};

static int find_drm_device(struct drm_device *dev) {
    drmModeRes *resources;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    int i, area;


    // Try to open the DRM device
    const char *card = getenv("DRM_CARD");
    if (!card) { card = "0"; }
    char device_path[64];
    snprintf(device_path, sizeof(device_path), "/dev/dri/card%s", card);
    dev->fd = open(device_path, O_RDWR | O_CLOEXEC);
    
    if (dev->fd < 0) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", device_path, strerror(errno));
        return -1;
    }

    // Get DRM resources
    resources = drmModeGetResources(dev->fd);
    if (!resources) {
        fprintf(stderr, "Error: Cannot get DRM resources\n");
        close(dev->fd);
        return -1;
    }

    dev->resources = resources;


    // Look for a connected connector
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(dev->fd, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
            break;
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }

    if (!connector) {
        fprintf(stderr, "Error: No connected connector found\n");
        drmModeFreeResources(resources);
        close(dev->fd);
        return -1;
    }

    dev->connector = connector;


    // Select the best mode (largest area)
    area = 0;
    for (i = 0; i < connector->count_modes; i++) {
        drmModeModeInfo *current_mode = &connector->modes[i];
        if (current_mode->hdisplay * current_mode->vdisplay > area) {
            dev->mode = *current_mode;
            area = current_mode->hdisplay * current_mode->vdisplay;
        }
    }

    // Find encoder
    if (connector->encoder_id) {
        encoder = drmModeGetEncoder(dev->fd, connector->encoder_id);
    }
    
    if (!encoder) {
        for (i = 0; i < resources->count_encoders; i++) {
            encoder = drmModeGetEncoder(dev->fd, resources->encoders[i]);
            if (encoder->encoder_id == connector->encoder_id) {
                break;
            }
            drmModeFreeEncoder(encoder);
            encoder = NULL;
        }
    }

    if (!encoder) {
        fprintf(stderr, "Error: No encoder found\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(dev->fd);
        return -1;
    }

    dev->encoder = encoder;


    // Get CRTC
    if (encoder->crtc_id) {
        dev->crtc = drmModeGetCrtc(dev->fd, encoder->crtc_id);
    }

    printf("Selected mode: %dx%d@%dHz\n", 
           dev->mode.hdisplay, dev->mode.vdisplay, dev->mode.vrefresh);

    return 0;
}

static int create_framebuffer(struct drm_device *dev) {
    struct drm_mode_create_dumb create_req = {0};
    struct drm_mode_map_dumb map_req = {0};
    int ret;


    // Create dumb buffer
    create_req.width = dev->mode.hdisplay;
    create_req.height = dev->mode.vdisplay;
    create_req.bpp = 32; // RGBA8888
    
    ret = drmIoctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_req);
    if (ret) {
        fprintf(stderr, "Error: Cannot create dumb buffer: %s\n", strerror(errno));
        return -1;
    }

    dev->handle = create_req.handle;
    dev->pitch = create_req.pitch;
    dev->size = create_req.size;


    // Create framebuffer
    ret = drmModeAddFB(dev->fd, dev->mode.hdisplay, dev->mode.vdisplay, 
                       24, 32, dev->pitch, dev->handle, &dev->fb_id);
    if (ret) {
        fprintf(stderr, "Error: Cannot create framebuffer: %s\n", strerror(errno));
        return -1;
    }


    // Map buffer to memory
    map_req.handle = dev->handle;
    ret = drmIoctl(dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req);
    if (ret) {
        fprintf(stderr, "Error: Cannot map buffer: %s\n", strerror(errno));
        return -1;
    }

    dev->map = mmap(0, dev->size, PROT_READ | PROT_WRITE, MAP_SHARED, 
                    dev->fd, map_req.offset);
    if (dev->map == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed: %s\n", strerror(errno));
        return -1;
    }

    // Clear buffer (black)
    memset(dev->map, 0, dev->size);

    return 0;
}

static void scale_and_center_image(unsigned char *image_data, int img_width, int img_height,
                                  uint32_t *fb_data, int fb_width, int fb_height) {
    // Calculate scale while maintaining aspect ratio
    float scale_x = (float)fb_width / img_width;
    float scale_y = (float)fb_height / img_height;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    
    int scaled_width = (int)(img_width * scale);
    int scaled_height = (int)(img_height * scale);
    
    // Center image
    int offset_x = (fb_width - scaled_width) / 2;
    int offset_y = (fb_height - scaled_height) / 2;
    
    printf("Scaling image from %dx%d to %dx%d, centered at %d,%d\n", 
           img_width, img_height, scaled_width, scaled_height, offset_x, offset_y);
    
    for (int y = 0; y < scaled_height; y++) {
        for (int x = 0; x < scaled_width; x++) {
            // Map from scaled coordinates to original image coordinates
            int src_x = (int)(x / scale);
            int src_y = (int)(y / scale);
            
            // Make sure we don't go out of bounds of the original image
            if (src_x >= img_width) src_x = img_width - 1;
            if (src_y >= img_height) src_y = img_height - 1;
            
            // Get pixel from original image (RGB)
            int src_idx = (src_y * img_width + src_x) * 3;
            unsigned char r = image_data[src_idx];
            unsigned char g = image_data[src_idx + 1];
            unsigned char b = image_data[src_idx + 2];
            
            // Write pixel to framebuffer (ARGB8888)
            int dst_x = offset_x + x;
            int dst_y = offset_y + y;
            if (dst_x < fb_width && dst_y < fb_height) {
                uint32_t pixel = (0xFF << 24) | (r << 16) | (g << 8) | b;
                fb_data[dst_y * fb_width + dst_x] = pixel;
            }
        }
    }
}

static int display_image(struct drm_device *dev, const char *image_path) {
    int img_width, img_height, channels;
    unsigned char *image_data;
    
    // Load image
    image_data = stbi_load(image_path, &img_width, &img_height, &channels, 3);
    if (!image_data) {
        fprintf(stderr, "Error: Cannot load image %s\n", image_path);
        return -1;
    }
    
    printf("Image loaded: %dx%d, %d channels\n", img_width, img_height, channels);
    
    // Clear framebuffer
    memset(dev->map, 0, dev->size);
    
    // Scale and copy image to framebuffer
    uint32_t *fb_data = (uint32_t *)dev->map;
    scale_and_center_image(image_data, img_width, img_height, 
                          fb_data, dev->mode.hdisplay, dev->mode.vdisplay);
    
    stbi_image_free(image_data);
    
    // Set mode and display framebuffer
    int ret = drmModeSetCrtc(dev->fd, dev->encoder->crtc_id, dev->fb_id, 
                            0, 0, &dev->connector->connector_id, 1, &dev->mode);
    if (ret) {
        fprintf(stderr, "Error: Cannot set CRTC: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

static void cleanup_drm(struct drm_device *dev) {
    if (dev->map && dev->map != MAP_FAILED) {
        munmap(dev->map, dev->size);
    }
    if (dev->fb_id) {
        drmModeRmFB(dev->fd, dev->fb_id);
    }
    if (dev->handle) {
        struct drm_mode_destroy_dumb destroy_req = {0};
        destroy_req.handle = dev->handle;
        drmIoctl(dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_req);
    }
    if (dev->crtc) {
        drmModeFreeCrtc(dev->crtc);
    }
    if (dev->encoder) {
        drmModeFreeEncoder(dev->encoder);
    }
    if (dev->connector) {
        drmModeFreeConnector(dev->connector);
    }
    if (dev->resources) {
        drmModeFreeResources(dev->resources);
    }
    if (dev->fd >= 0) {
        close(dev->fd);
    }
}

int main(int argc, char *argv[]) {
    struct drm_device dev = {0};
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image_path>\n", argv[0]);
        return 1;
    }
    
    printf("Starting DRM image viewer...\n");
    
    // Initialize DRM device
    if (find_drm_device(&dev) != 0) {
        return 1;
    }
    
    // Create framebuffer
    if (create_framebuffer(&dev) != 0) {
        cleanup_drm(&dev);
        return 1;
    }
    
    // Display image
    if (display_image(&dev, argv[1]) != 0) {
        cleanup_drm(&dev);
        return 1;
    }
    
    printf("Image displayed. Press Enter to exit...\n");
    getchar();
    
    cleanup_drm(&dev);
    printf("Cleanup completed.\n");
    
    return 0;
}
