/*
 * Host-side byte-level NTFS boot-record regression for PowerPC OpenBIOS.
 * The parser is the one used by fsys_ntfs.c; no NT boot success implied.
 */
#include <assert.h>
#include <string.h>
#include "../fs/grubfs/ntfs_boot.h"

static void put32(unsigned char *p, unsigned int n)
{
    p[0] = (unsigned char)n;
    p[1] = (unsigned char)(n >> 8);
    p[2] = (unsigned char)(n >> 16);
    p[3] = (unsigned char)(n >> 24);
}

static void make_ntfs(unsigned char sector[512])
{
    memset(sector, 0, 512);
    sector[0] = 0xeb;
    memcpy(sector + 3, "NTFS    ", 8);
    ntfs_boot_put_le16(sector + 0x0b, 512);
    sector[0x0d] = 8;                  /* 4 KiB clusters */
    put32(sector + 0x30, 64);          /* MFT starts at LCN 64 */
    sector[0x40] = 0xf6;               /* 2^10 = 1024-byte MFT */
    sector[0x44] = 0xf4;               /* 2^12 = 4096-byte index */
    sector[510] = 0x55;
    sector[511] = 0xaa;
}

int main(void)
{
    unsigned char s[512];
    struct ntfs_boot_geometry g;
    make_ntfs(s);
    assert(ntfs_boot_decode(s, &g));
    assert(g.cluster_size == 4096);
    assert(g.mft_record_size == 1024);
    assert(g.index_record_size == 4096);
    assert(g.mft_first_sector == 512);
    assert(ntfs_boot_le32(s + 0x30) == 64);

    /* Reject invalid and unsupported VBRs before disk offsets are derived. */
    make_ntfs(s); s[3] = 'F'; assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); s[0x0d] = 0; assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); s[0x0d] = 3; assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); ntfs_boot_put_le16(s + 0x0b, 4096);
    assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); s[0x40] = 0; assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); s[0x40] = 0xf1; assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); s[0x44] = 0xef; assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); s[0x34] = 1; assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); put32(s + 0x30, 0xffffffffu);
    assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); s[510] = 0; assert(!ntfs_boot_decode(s, &g));
    make_ntfs(s); s[0] = 0; assert(!ntfs_boot_decode(s, &g));

    /* Positive cluster-count encoding is valid when within buffer bounds. */
    make_ntfs(s);
    s[0x0d] = 1;
    s[0x40] = 2; s[0x44] = 8;
    assert(ntfs_boot_decode(s, &g));
    assert(g.mft_record_size == 1024 && g.index_record_size == 4096);
    return 0;
}
