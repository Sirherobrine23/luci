#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <stdint.h>

struct link_mode_def {
    int bit;
    int speed;
    const char *duplex;
};

// Based on standard ETHTOOL_LINK_MODE_*_BIT
static const struct link_mode_def link_modes[] = {
    {0, 10, "half"},
    {1, 10, "full"},
    {2, 100, "half"},
    {3, 100, "full"},
    {4, 1000, "half"},
    {5, 1000, "full"},
    {12, 10000, "full"},
    {15, 2500, "full"},
    {17, 1000, "full"},
    {18, 10000, "full"},
    {19, 10000, "full"},
    {21, 20000, "full"},
    {22, 20000, "full"},
    {23, 40000, "full"},
    {24, 40000, "full"},
    {25, 40000, "full"},
    {26, 40000, "full"},
    {27, 56000, "full"},
    {28, 56000, "full"},
    {29, 56000, "full"},
    {30, 56000, "full"},
    {31, 25000, "full"},
    {32, 25000, "full"},
    {33, 25000, "full"},
    {34, 50000, "full"},
    {35, 50000, "full"},
    {36, 100000, "full"},
    {37, 100000, "full"},
    {38, 100000, "full"},
    {39, 100000, "full"},
    {40, 50000, "full"},
    {41, 1000, "full"},
    {42, 10000, "full"},
    {43, 10000, "full"},
    {44, 10000, "full"},
    {45, 10000, "full"},
    {46, 10000, "full"},
    {47, 2500, "full"},
    {48, 5000, "full"},
    {52, 50000, "full"},
    {53, 50000, "full"},
    {54, 50000, "full"},
    {55, 50000, "full"},
    {56, 50000, "full"},
    {57, 100000, "full"},
    {58, 100000, "full"},
    {59, 100000, "full"},
    {60, 100000, "full"},
    {61, 100000, "full"},
    {62, 200000, "full"},
    {63, 200000, "full"},
    {64, 200000, "full"},
    {65, 200000, "full"},
    {66, 200000, "full"},
    {67, 100, "full"},
    {68, 1000, "full"},
    {69, 400000, "full"},
    {70, 400000, "full"},
    {71, 400000, "full"},
    {72, 400000, "full"},
    {73, 400000, "full"},
    {75, 100000, "full"},
    {76, 100000, "full"},
    {77, 100000, "full"},
    {78, 100000, "full"},
    {79, 100000, "full"},
    {80, 200000, "full"},
    {81, 200000, "full"},
    {82, 200000, "full"},
    {83, 200000, "full"},
    {84, 200000, "full"},
    {85, 400000, "full"},
    {86, 400000, "full"},
    {87, 400000, "full"},
    {88, 400000, "full"},
    {89, 400000, "full"},
    {90, 100, "half"},
    {91, 100, "full"},
    {92, 10, "full"},
    {93, 800000, "full"},
    {94, 800000, "full"},
    {95, 800000, "full"},
    {96, 800000, "full"},
    {97, 800000, "full"},
    {98, 800000, "full"},
    {99, 10, "full"},
    {100, 10, "half"},
    {101, 10, "half"},
    {102, 10, "full"},
    {103, 200000, "full"},
    {104, 200000, "full"},
    {105, 200000, "full"},
    {106, 200000, "full"},
    {107, 200000, "full"},
    {108, 200000, "full"},
    {109, 400000, "full"},
    {110, 400000, "full"},
    {111, 400000, "full"},
    {112, 400000, "full"},
    {113, 400000, "full"},
    {114, 400000, "full"},
    {115, 800000, "full"},
    {116, 800000, "full"},
    {117, 800000, "full"},
    {118, 800000, "full"},
    {119, 800000, "full"},
    {120, 800000, "full"},
    {121, 1600000, "full"},
    {122, 1600000, "full"},
    {123, 1600000, "full"},
    {124, 1600000, "full"}
};
#define NUM_LINK_MODES (sizeof(link_modes) / sizeof(link_modes[0]))

void print_json_mode(int speed, const char *duplex, int is_first) {
    if (!is_first) printf(", ");
    printf("{\"speed\": %d, \"duplex\": \"%s\"}", speed, duplex);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);

    struct {
        struct ethtool_link_settings req;
        __u32 link_mode_data[3 * 127];
    } ecmd;

    memset(&ecmd, 0, sizeof(ecmd));
    ecmd.req.cmd = ETHTOOL_GLINKSETTINGS;
    ifr.ifr_data = (caddr_t)&ecmd;

    if (ioctl(fd, SIOCETHTOOL, &ifr) < 0) {
        // Fallback to older ETHTOOL_GSET
        struct ethtool_cmd ecmd_old;
        memset(&ecmd_old, 0, sizeof(ecmd_old));
        ecmd_old.cmd = ETHTOOL_GSET;
        ifr.ifr_data = (caddr_t)&ecmd_old;

        if (ioctl(fd, SIOCETHTOOL, &ifr) == 0) {
            int first = 1;
            printf("[");
            for (size_t i = 0; i < NUM_LINK_MODES; i++) {
                if (link_modes[i].bit < 32 && (ecmd_old.supported & (1U << link_modes[i].bit))) {
                    print_json_mode(link_modes[i].speed, link_modes[i].duplex, first);
                    first = 0;
                }
            }
            printf("]\n");
        } else {
            printf("[]\n");
        }
    } else {
        if (ecmd.req.link_mode_masks_nwords < 0) {
            ecmd.req.link_mode_masks_nwords = -ecmd.req.link_mode_masks_nwords;
            if (ioctl(fd, SIOCETHTOOL, &ifr) == 0) {
                // link_mode_data contains 3 bitmasks: supported, advertising, lp_advertising
                // The supported mask is at offset 0, which spans multiple 32-bit words
                int first = 1;
                printf("[");
                for (size_t i = 0; i < NUM_LINK_MODES; i++) {
                    int bit = link_modes[i].bit;
                    int word_index = bit / 32;
                    int bit_index = bit % 32;

                    if (word_index < ecmd.req.link_mode_masks_nwords &&
                        (ecmd.link_mode_data[word_index] & (1U << bit_index))) {
                        print_json_mode(link_modes[i].speed, link_modes[i].duplex, first);
                        first = 0;
                    }
                }
                printf("]\n");
            } else {
                printf("[]\n");
            }
        } else {
            printf("[]\n");
        }
    }

    close(fd);
    return 0;
}
