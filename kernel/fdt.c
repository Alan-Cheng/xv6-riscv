#include "types.h"

// DTB integers are big-endian, including on little-endian RISC-V.
static uint
be32(const uchar *p)
{
  return (uint)p[0] << 24 | (uint)p[1] << 16 | (uint)p[2] << 8 | p[3];
}

static int
equals(const uchar *p, uint n, const char *s)
{
  for (uint i = 0; i < n; i++) {
    if (p[i] != (uchar)s[i])
      return 0;
    if (s[i] == 0)
      return 1;
  }
  return 0;
}

// Sum root-level memory nodes' reg sizes. Supports one- or two-cell
// addresses/sizes, multiple banks, and v17-compatible DTBs (QEMU virt).
// Call while the boot DTB is still accessible, before freeing physical pages.
// Returns -1 for unsupported/malformed data or no memory; leaves output alone.
int
fdt_memory_size(uint64 dtb, uint64 *bytes)
{
  const uchar *b = (const uchar *)dtb;
  if (!b || be32(b) != 0xd00dfeed)
    return -1;
  uint total = be32(b + 4);
  if (total < 40 || be32(b + 20) < 17 || be32(b + 24) > 17)
    return -1;
  uint off = be32(b + 8), strings = be32(b + 12);
  uint slen = be32(b + 32), len = be32(b + 36);
  if (off < 40 || (off & 3) || off > total || len > total - off ||
      strings < 40 || strings > total || slen > total - strings)
    return -1;
  uint end = off + len;
  uint ac = 2, sc = 1, reglen = 0;
  const uchar *reg = 0;
  int depth = 0, memory = 0, root_seen = 0;
  uint64 sum = 0;
  while (off <= end && end - off >= 4) {
    uint token = be32(b + off);
    off += 4;
    if (token == 1) { // FDT_BEGIN_NODE
      if (depth == 0 && (root_seen++ || off == end || b[off] != 0))
        return -1;
      while (off < end && b[off])
        off++;
      if (off == end)
        return -1;
      off++;
      depth++;
      if (depth == 2) {
        memory = 0;
        reg = 0;
        reglen = 0;
      }
    } else if (token == 2) { // FDT_END_NODE
      if (depth <= 0)
        return -1;
      if (depth == 2 && memory) {
        if (ac < 1 || ac > 2 || sc < 1 || sc > 2)
          return -1;
        uint stride = 4 * (ac + sc);
        if (!reg || !reglen || reglen % stride)
          return -1;
        for (uint i = 0; i < reglen; i += stride) {
          uint64 size = 0;
          for (uint j = 0; j < sc; j++)
            size = (size << 32) | be32(reg + i + 4 * (ac + j));
          if (size > ~(uint64)0 - sum)
            return -1;
          sum += size;
        }
      }
      depth--;
    } else if (token == 3) { // FDT_PROP
      if (!depth || end - off < 8)
        return -1;
      uint n = be32(b + off), name = be32(b + off + 4);
      off += 8;
      if (n > end - off || name >= slen)
        return -1;
      const uchar *key = b + strings + name;
      uint available = slen - name, k = 0;
      while (k < available && key[k])
        k++;
      if (k == available)
        return -1;
      if (depth == 1 && equals(key, available, "#address-cells")) {
        if (n != 4)
          return -1;
        ac = be32(b + off);
      } else if (depth == 1 && equals(key, available, "#size-cells")) {
        if (n != 4)
          return -1;
        sc = be32(b + off);
      } else if (depth == 2 && equals(key, available, "device_type")) {
        memory = equals(b + off, n, "memory");
      } else if (depth == 2 && equals(key, available, "reg")) {
        reg = b + off;
        reglen = n;
      }
      off += n;
    } else if (token == 9) { // FDT_END
      if (depth || !root_seen || !sum || off != end)
        return -1;
      *bytes = sum;
      return 0;
    } else if (token != 4) { // FDT_NOP
      return -1;
    }
    uint padding = (4 - (off & 3)) & 3;
    if (padding > end - off)
      return -1;
    off += padding;
  }
  return -1;
}
