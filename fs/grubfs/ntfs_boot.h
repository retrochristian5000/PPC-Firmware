/*
 * Read-only NTFS volume boot-record decoder for the legacy GRUB filesystem.
 *
 * This deliberately has no OpenBIOS or host-endianness dependencies.  It
 * verifies only the geometry the legacy read path can safely represent; it
 * is NOT a complete NTFS filesystem validator or ARC/NT bootloader.
 */
#ifndef OPENBIOS_GRUBFS_NTFS_BOOT_H
#define OPENBIOS_GRUBFS_NTFS_BOOT_H

struct ntfs_boot_geometry {
    unsigned int bytes_per_sector;
    unsigned int sectors_per_cluster;
    unsigned int cluster_size;
    unsigned int mft_record_size;
    unsigned int index_record_size;
    unsigned int mft_first_sector;
};

static inline unsigned int ntfs_boot_le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static inline unsigned int ntfs_boot_le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static inline void ntfs_boot_put_le16(unsigned char *p, unsigned int val)
{
    p[0] = (unsigned char)val;
    p[1] = (unsigned char)(val >> 8);
}

/* NTFS record lengths are cluster multiples (positive) or powers of two
 * (negative signed 8-bit exponent). Reject layouts not handled by GRUB's
 * fixed MFT/index buffers before any disk read can use those sizes.
 */
static inline int ntfs_boot_record_size(unsigned char raw,
                                        unsigned int cluster_size,
                                        unsigned int max_size,
                                        unsigned int *result)
{
    unsigned int size;

    if (raw == 0)
        return 0;
    if (raw & 0x80) {
        unsigned int exponent = 256u - (unsigned int)raw;
        if (exponent >= 32 || exponent > 14)
            return 0;
        size = 1u << exponent;
    } else {
        if ((unsigned int)raw > max_size / cluster_size)
            return 0;
        size = (unsigned int)raw * cluster_size;
    }
    if (size < 512 || size > max_size || size % 512)
        return 0;
    *result = size;
    return 1;
}

/* Only legacy 512-byte-sector NTFS volumes with 32-bit MFT placement are
 * admitted: these are explicit limitations of fsys_ntfs.c, not NTFS itself.
 */
static inline int ntfs_boot_decode(const unsigned char sector[512],
                                   struct ntfs_boot_geometry *out)
{
    unsigned int i, bytes_per_sector, spc, cluster_size, mft_lcn;
    unsigned int mft_size, index_size;

    if (!sector || !out)
        return 0;
    if (sector[0] != 0xeb && sector[0] != 0xe9)
        return 0;
    {
        static const unsigned char label[8] = {
            'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '
        };
        for (i = 0; i < 8; ++i)
            if (sector[3 + i] != label[i])
                return 0;
    }
    if (sector[510] != 0x55 || sector[511] != 0xaa)
        return 0;

    bytes_per_sector = ntfs_boot_le16(sector + 0x0b);
    spc = sector[0x0d];
    if (bytes_per_sector != 512 || !spc || spc > 128 ||
        (spc & (spc - 1)))
        return 0;
    cluster_size = bytes_per_sector * spc;

    if (!ntfs_boot_record_size(sector[0x40], cluster_size, 1024,
                               &mft_size) ||
        !ntfs_boot_record_size(sector[0x44], cluster_size, 16384,
                               &index_size))
        return 0;
    /* 64-bit MFT LCN on disk; the old reader supports only 32-bit sectors. */
    if (ntfs_boot_le32(sector + 0x34) != 0)
        return 0;
    mft_lcn = ntfs_boot_le32(sector + 0x30);
    if (!mft_lcn || mft_lcn > (0xffffffffu - mft_size / 512u) / spc)
        return 0;

    out->bytes_per_sector = bytes_per_sector;
    out->sectors_per_cluster = spc;
    out->cluster_size = cluster_size;
    out->mft_record_size = mft_size;
    out->index_record_size = index_size;
    out->mft_first_sector = mft_lcn * spc;
    return 1;
}

#endif /* OPENBIOS_GRUBFS_NTFS_BOOT_H */
