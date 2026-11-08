/*
 * Low-level protocol for the Logitech G Pro keyboard (046d:c339).
 *
 * The keyboard exposes two USB interfaces. Interface 0 is the boot keyboard;
 * interface 1 carries media keys plus a vendor page (0xFF43) with two output
 * reports used for lighting:
 *   0x11  "long"      20 bytes
 *   0x12  "very long" 64 bytes
 *
 * Commands (byte layout taken from the g810-led project):
 *   set keys : 11|12 ff 0c 3a 00 <group> 00 <n>  then n * (code r g b)
 *   commit   : 11 ff 0c 5a 00 ...
 */
#define _DEFAULT_SOURCE
#include "gpro.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define REPORT_SHORT_ID  0x11
#define REPORT_SHORT_LEN 20
#define REPORT_LONG_ID   0x12
#define REPORT_LONG_LEN  64
#define HEADER_LEN       8
#define KEY_ENTRY_LEN    4

/* Does this report descriptor contain the vendor page 0xFF43 with report 0x11? */
static int is_lighting_interface(int fd)
{
    int size = 0;
    struct hidraw_report_descriptor rd;

    if (ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0 || size <= 0)
        return 0;
    rd.size = (uint32_t)size;
    if (ioctl(fd, HIDIOCGRDESC, &rd) < 0)
        return 0;

    int vendor_page = 0, report_11 = 0;
    for (uint32_t i = 0; i + 2 < rd.size; i++) {
        if (rd.value[i] == 0x06 && rd.value[i + 1] == 0x43 && rd.value[i + 2] == 0xff)
            vendor_page = 1;
        if (rd.value[i] == 0x85 && rd.value[i + 1] == REPORT_SHORT_ID)
            report_11 = 1;
    }
    return vendor_page && report_11;
}

/* Calls cb for every hidraw node of the keyboard. Stops when cb returns non-zero. */
static int for_each_gpro_node(int (*cb)(const char *path, int fd, void *ctx), void *ctx)
{
    DIR *dir = opendir("/dev");
    if (!dir)
        return -1;

    struct dirent *ent;
    int ret = 0, seen_eacces = 0;
    while ((ent = readdir(dir)) && !ret) {
        if (strncmp(ent->d_name, "hidraw", 6) != 0)
            continue;

        char path[300];
        snprintf(path, sizeof path, "/dev/%s", ent->d_name);

        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            if (errno == EACCES)
                seen_eacces = 1;
            continue;
        }

        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0 &&
            (uint16_t)info.vendor == GPRO_VID && (uint16_t)info.product == GPRO_PID) {
            ret = cb(path, fd, ctx);
            if (ret)
                break; /* cb took ownership of fd */
        }
        close(fd);
    }
    closedir(dir);

    if (!ret && seen_eacces)
        fprintf(stderr, "note: some /dev/hidraw* nodes were not accessible "
                        "(install the udev rule or run as root)\n");
    return ret;
}

static int open_cb(const char *path, int fd, void *ctx)
{
    gpro_dev *dev = ctx;
    if (!is_lighting_interface(fd))
        return 0;
    dev->fd = fd;
    snprintf(dev->path, sizeof dev->path, "%s", path);
    return 1;
}

int gpro_open(gpro_dev *dev)
{
    dev->fd = -1;
    dev->path[0] = '\0';
    if (for_each_gpro_node(open_cb, dev) != 1) {
        fprintf(stderr, "error: G Pro lighting interface not found\n");
        return -1;
    }
    return 0;
}

void gpro_close(gpro_dev *dev)
{
    if (dev->fd >= 0)
        close(dev->fd);
    dev->fd = -1;
}

static int list_cb(const char *path, int fd, void *ctx)
{
    (void)ctx;
    printf("%s%s\n", path, is_lighting_interface(fd) ? "  (lighting interface)" : "");
    return 0;
}

void gpro_list(void)
{
    for_each_gpro_node(list_cb, NULL);
}

static int send_report(gpro_dev *dev, const uint8_t *buf, size_t len)
{
    ssize_t n = write(dev->fd, buf, len);
    if (n < 0) {
        perror("write");
        return -1;
    }
    if ((size_t)n != len) {
        fprintf(stderr, "error: short write (%zd/%zu)\n", n, len);
        return -1;
    }
    return 0;
}

int gpro_set_keys(gpro_dev *dev, enum gpro_group group,
                  const struct gpro_key *keys, size_t count)
{
    /* Regular keys go in 64-byte reports (14 keys each), others in 20-byte ones (3 each). */
    int long_report  = (group == GPRO_GROUP_KEYS);
    size_t len       = long_report ? REPORT_LONG_LEN : REPORT_SHORT_LEN;
    size_t per_frame = (len - HEADER_LEN) / KEY_ENTRY_LEN;

    for (size_t off = 0; off < count; off += per_frame) {
        size_t n = count - off < per_frame ? count - off : per_frame;
        uint8_t buf[REPORT_LONG_LEN] = {0};

        buf[0] = long_report ? REPORT_LONG_ID : REPORT_SHORT_ID;
        buf[1] = 0xff;
        buf[2] = 0x0c;
        buf[3] = 0x3a;
        buf[4] = 0x00;
        buf[5] = (uint8_t)group;
        buf[6] = 0x00;
        buf[7] = (uint8_t)n;

        for (size_t i = 0; i < n; i++) {
            uint8_t *e = &buf[HEADER_LEN + i * KEY_ENTRY_LEN];
            e[0] = keys[off + i].code;
            e[1] = keys[off + i].color.r;
            e[2] = keys[off + i].color.g;
            e[3] = keys[off + i].color.b;
        }

        if (send_report(dev, buf, len) < 0)
            return -1;
    }
    return 0;
}

int gpro_set_all(gpro_dev *dev, struct gpro_color c)
{
    /*
     * Key codes are HID keyboard usage IDs: 0x04 (A) .. 0x65 (Menu), plus the
     * modifiers at 0xe0 .. 0xe7 (checked on the keyboard). Codes with no LED
     * (keys a TKL doesn't have) are ignored by the keyboard.
     */
    struct gpro_key keys[(0x65 - 0x04 + 1) + 8];
    size_t n = 0;
    for (unsigned code = 0x04; code <= 0x65; code++)
        keys[n++] = (struct gpro_key){ .code = (uint8_t)code, .color = c };
    for (unsigned code = 0xe0; code <= 0xe7; code++)
        keys[n++] = (struct gpro_key){ .code = (uint8_t)code, .color = c };

    if (gpro_set_keys(dev, GPRO_GROUP_KEYS, keys, n) < 0)
        return -1;

    struct gpro_key logo = { .code = 0x01, .color = c };
    return gpro_set_keys(dev, GPRO_GROUP_LOGO, &logo, 1);
}

int gpro_commit(gpro_dev *dev)
{
    uint8_t buf[REPORT_SHORT_LEN] = { REPORT_SHORT_ID, 0xff, 0x0c, 0x5a };
    return send_report(dev, buf, sizeof buf);
}

int gpro_parse_color(const char *s, struct gpro_color *out)
{
    if (*s == '#')
        s++;
    if (strlen(s) != 6)
        return -1;

    char *end;
    unsigned long v = strtoul(s, &end, 16);
    if (*end != '\0')
        return -1;

    out->r = (v >> 16) & 0xff;
    out->g = (v >> 8) & 0xff;
    out->b = v & 0xff;
    return 0;
}
