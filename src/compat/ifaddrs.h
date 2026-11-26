/* SPDX-License-Identifier: MPL-2.0 */
/* The NBT toolchain's QNX headers have no <ifaddrs.h> yet: getifaddrs() as
 * QNX 6.5 SP1 declares it (libsocket.so.3 exports getifaddrs and freeifaddrs),
 * for NBT-AAD's cluster media sender (src/cluster/ric_media.cpp, v1.9.120).
 * Only the daemon's compile sees this folder (Makefile, AAD_INC). The structure
 * below was compared with the QNX 6.5 SP1 header (2026-11-26). */
#ifndef NBT_COMPAT_IFADDRS_H
#define NBT_COMPAT_IFADDRS_H

#include <sys/types.h>

struct sockaddr;

struct ifaddrs {
    struct ifaddrs *ifa_next;
    char *ifa_name;
    unsigned int ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    struct sockaddr *ifa_dstaddr;
    void *ifa_data;
};

#ifndef ifa_broadaddr
#define ifa_broadaddr ifa_dstaddr
#endif

#ifdef __cplusplus
extern "C" {
#endif
int getifaddrs(struct ifaddrs **list);
void freeifaddrs(struct ifaddrs *list);
#ifdef __cplusplus
}
#endif

#endif
