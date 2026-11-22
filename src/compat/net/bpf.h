/* SPDX-License-Identifier: MPL-2.0 */
/* The NBT toolchain's QNX headers have no <net/bpf.h> yet: io-pkt's packet
 * filter (/dev/bpf, classic BPF) as QNX 6.5 SP1 defines it, for NBT-AAD's
 * packet readers (src/gesture_sniff.cpp, src/cluster/ric_media.cpp). Only the
 * daemon's compile sees this folder (Makefile, AAD_INC). The values, sizes and
 * offsets below were compared with the QNX 6.5 SP1 headers' (2026-11-22). */
#ifndef NBT_COMPAT_NET_BPF_H
#define NBT_COMPAT_NET_BPF_H

#include <net/if.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define BPF_ALIGNMENT sizeof(long)
#define BPF_WORDALIGN(x) (((x) + (BPF_ALIGNMENT - 1)) & ~(BPF_ALIGNMENT - 1))

/* Link types (BIOCGDLT). */
#define DLT_NULL 0
#define DLT_EN10MB 1

struct bpf_insn {
    uint16_t code;
    unsigned char jt;
    unsigned char jf;
    uint32_t k;
};

struct bpf_program {
    unsigned int bf_len;
    struct bpf_insn *bf_insns;
};

/* Each captured packet starts with this header; bh_hdrlen is its length as
 * the kernel wrote it (not sizeof, which the compiler pads). */
struct bpf_hdr {
    struct timeval bh_tstamp;
    uint32_t bh_caplen;
    uint32_t bh_datalen;
    uint16_t bh_hdrlen;
};

#define BIOCGBLEN _IOR('B', 102, unsigned int)
#define BIOCSBLEN _IOWR('B', 102, unsigned int)
#define BIOCSETF _IOW('B', 103, struct bpf_program)
#define BIOCFLUSH _IO('B', 104)
#define BIOCPROMISC _IO('B', 105)
#define BIOCGDLT _IOR('B', 106, unsigned int)
#define BIOCGETIF _IOR('B', 107, struct ifreq)
#define BIOCSETIF _IOW('B', 108, struct ifreq)
#define BIOCSRTIMEOUT _IOW('B', 109, struct timeval)
#define BIOCGRTIMEOUT _IOR('B', 110, struct timeval)
#define BIOCIMMEDIATE _IOW('B', 112, unsigned int)

#endif
