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

void print_json_mode(int speed, const char *duplex, int is_first) {
    if (!is_first) printf(", ");
    printf("{\"speed\": %d, \"duplex\": \"%s\"}", speed, duplex);
}

void print_link_modes(uint32_t mask) {
    int first = 1;
    printf("[");
    if (mask & (1 << ETHTOOL_LINK_MODE_10baseT_Half_BIT)) { print_json_mode(10, "half", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_10baseT_Full_BIT)) { print_json_mode(10, "full", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_100baseT_Half_BIT)) { print_json_mode(100, "half", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_100baseT_Full_BIT)) { print_json_mode(100, "full", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_1000baseT_Half_BIT)) { print_json_mode(1000, "half", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_1000baseT_Full_BIT)) { print_json_mode(1000, "full", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_10000baseT_Full_BIT)) { print_json_mode(10000, "full", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_2500baseX_Full_BIT)) { print_json_mode(2500, "full", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_1000baseKX_Full_BIT)) { print_json_mode(1000, "full", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT)) { print_json_mode(10000, "full", first); first = 0; }
    if (mask & (1 << ETHTOOL_LINK_MODE_10000baseKR_Full_BIT)) { print_json_mode(10000, "full", first); first = 0; }
    printf("]\n");
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
            if (ecmd_old.supported & SUPPORTED_10baseT_Half) { print_json_mode(10, "half", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_10baseT_Full) { print_json_mode(10, "full", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_100baseT_Half) { print_json_mode(100, "half", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_100baseT_Full) { print_json_mode(100, "full", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_1000baseT_Half) { print_json_mode(1000, "half", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_1000baseT_Full) { print_json_mode(1000, "full", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_10000baseT_Full) { print_json_mode(10000, "full", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_2500baseX_Full) { print_json_mode(2500, "full", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_1000baseKX_Full) { print_json_mode(1000, "full", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_10000baseKX4_Full) { print_json_mode(10000, "full", first); first = 0; }
            if (ecmd_old.supported & SUPPORTED_10000baseKR_Full) { print_json_mode(10000, "full", first); first = 0; }
            printf("]\n");
        } else {
            printf("[]\n");
        }
    } else {
        if (ecmd.req.link_mode_masks_nwords < 0) {
            ecmd.req.link_mode_masks_nwords = -ecmd.req.link_mode_masks_nwords;
            if (ioctl(fd, SIOCETHTOOL, &ifr) == 0) {
                // link_mode_data contains 3 bitmasks: supported, advertising, lp_advertising
                // The supported mask is at offset 0.
                print_link_modes(ecmd.link_mode_data[0]);
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
