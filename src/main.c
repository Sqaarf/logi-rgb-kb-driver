#include "gpro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_KEYS  128
#define MAX_LINES 256

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage:\n"
            "  %s list                     list the keyboard's hidraw nodes\n"
            "  %s all  <rrggbb>            set every key and the logo\n"
            "  %s logo <rrggbb>            set the logo\n"
            "  %s code <hex> <rrggbb>      set one key by raw key code (e.g. 04 = A)\n"
            "  %s keys <entry>...          set many keys at once\n"
            "  %s load <file>              apply a profile: one entry per line, # comments\n"
            "\n"
            "entries: <hex code>=<rrggbb>, logo=<rrggbb>, all=<rrggbb> (applied first)\n",
            prog, prog, prog, prog, prog, prog);
}

/* A set of colors to apply in one commit. */
struct spec {
    int has_all, has_logo;
    struct gpro_color all;
    struct gpro_key logo;
    struct gpro_key keys[MAX_KEYS];
    size_t count;
};

/* Add one "name=rrggbb" entry. Modifies the string. */
static int spec_add(struct spec *s, char *entry)
{
    char *eq = strchr(entry, '=');
    if (!eq) {
        fprintf(stderr, "error: expected <code>=<rrggbb>, got '%s'\n", entry);
        return -1;
    }
    *eq = '\0';
    const char *name = entry, *value = eq + 1;

    struct gpro_color c;
    if (gpro_parse_color(value, &c) < 0) {
        fprintf(stderr, "error: invalid color '%s' for '%s'\n", value, name);
        return -1;
    }

    if (strcmp(name, "all") == 0) {
        s->all = c;
        s->has_all = 1;
        return 0;
    }
    if (strcmp(name, "logo") == 0) {
        s->logo = (struct gpro_key){ .code = 0x01, .color = c };
        s->has_logo = 1;
        return 0;
    }

    char *end;
    unsigned long code = strtoul(name, &end, 16);
    if (*name == '\0' || *end != '\0' || code > 0xff) {
        fprintf(stderr, "error: invalid key code '%s'\n", name);
        return -1;
    }
    if (s->count == MAX_KEYS) {
        fprintf(stderr, "error: too many keys (max %d)\n", MAX_KEYS);
        return -1;
    }
    s->keys[s->count++] = (struct gpro_key){ .code = (uint8_t)code, .color = c };
    return 0;
}

static int spec_load(struct spec *s, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }

    char line[128];
    int n = 0, rc = 0;
    while (rc == 0 && fgets(line, sizeof line, f)) {
        if (++n > MAX_LINES) {
            fprintf(stderr, "error: %s: too many lines\n", path);
            rc = -1;
            break;
        }
        /* Strip comments and surrounding whitespace. */
        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';
        char *start = line + strspn(line, " \t");
        char *stop = start + strcspn(start, " \t\r\n");
        *stop = '\0';
        if (*start)
            rc = spec_add(s, start);
    }
    fclose(f);
    return rc;
}

/* Send everything in the spec, then commit once. */
static int spec_apply(const struct spec *s)
{
    if (!s->has_all && !s->has_logo && !s->count) {
        fprintf(stderr, "error: nothing to set\n");
        return -1;
    }

    gpro_dev dev;
    if (gpro_open(&dev) < 0)
        return -1;

    int rc = 0;
    if (s->has_all)
        rc = gpro_set_all(&dev, s->all);
    if (rc == 0 && s->count)
        rc = gpro_set_keys(&dev, GPRO_GROUP_KEYS, s->keys, s->count);
    if (rc == 0 && s->has_logo)
        rc = gpro_set_keys(&dev, GPRO_GROUP_LOGO, &s->logo, 1);
    if (rc == 0)
        rc = gpro_commit(&dev);

    gpro_close(&dev);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    static struct spec s; /* static: zero-initialized and off the stack */

    if (strcmp(cmd, "list") == 0 && argc == 2) {
        gpro_list();
        return 0;
    }

    if (strcmp(cmd, "load") == 0 && argc == 3) {
        if (spec_load(&s, argv[2]) < 0)
            return 1;
    } else if (strcmp(cmd, "keys") == 0 && argc >= 3) {
        for (int i = 2; i < argc; i++)
            if (spec_add(&s, argv[i]) < 0)
                return 1;
    } else if ((strcmp(cmd, "all") == 0 || strcmp(cmd, "logo") == 0) && argc == 3) {
        char entry[64];
        snprintf(entry, sizeof entry, "%s=%s", cmd, argv[2]);
        if (spec_add(&s, entry) < 0)
            return 1;
    } else if (strcmp(cmd, "code") == 0 && argc == 4) {
        char entry[64];
        snprintf(entry, sizeof entry, "%s=%s", argv[2], argv[3]);
        if (spec_add(&s, entry) < 0)
            return 1;
    } else {
        usage(argv[0]);
        return 1;
    }

    return spec_apply(&s) == 0 ? 0 : 1;
}
