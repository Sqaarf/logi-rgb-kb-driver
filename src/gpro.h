#ifndef GPRO_H
#define GPRO_H

#include <stddef.h>
#include <stdint.h>

#define GPRO_VID 0x046d
#define GPRO_PID 0xc339

/* Lighting groups (byte 5 of a 0x3a "set keys" command). */
enum gpro_group {
    GPRO_GROUP_KEYS       = 0x01, /* regular keys, sent in 64-byte 0x12 reports */
    GPRO_GROUP_MULTIMEDIA = 0x02,
    GPRO_GROUP_LOGO       = 0x10,
    GPRO_GROUP_INDICATORS = 0x40,
};

struct gpro_color {
    uint8_t r, g, b;
};

struct gpro_key {
    uint8_t code;            /* key code inside its group */
    struct gpro_color color;
};

typedef struct {
    int fd;
    char path[300];
} gpro_dev;

/* Find and open the keyboard's lighting (HID++) interface. Returns 0 on success. */
int gpro_open(gpro_dev *dev);
void gpro_close(gpro_dev *dev);

/* Print every hidraw node belonging to the keyboard. */
void gpro_list(void);

/* Queue colors for keys in a group. Nothing changes until gpro_commit(). */
int gpro_set_keys(gpro_dev *dev, enum gpro_group group,
                  const struct gpro_key *keys, size_t count);

/* Set every regular key (and the logo) to one color. Not committed. */
int gpro_set_all(gpro_dev *dev, struct gpro_color c);

/* Apply all queued color changes. */
int gpro_commit(gpro_dev *dev);

/* Parse "rrggbb" or "#rrggbb". Returns 0 on success. */
int gpro_parse_color(const char *s, struct gpro_color *out);

#endif
