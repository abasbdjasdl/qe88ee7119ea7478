#include "../src/ReportBuffer.hpp"
#include <cassert>
#include <cstring>
int main() {
    rtl8852be::ReportBuffer<1> empty;
    empty.append("x");
    assert(empty.clipped && empty.used == 0 && empty.bytes[0] == 0);
    rtl8852be::ReportBuffer<5> b;
    b.append(nullptr);
    const uint8_t x[] = {0x00, 0xff};
    b.hex(x, 2);
    assert(!b.clipped && !strcmp(b.bytes, "00ff"));
    b.append("X");
    assert(b.clipped && b.used == 4 && !strcmp(b.bytes, "00ff"));
    rtl8852be::ReportBuffer<2048> report;
    uint8_t pci[256]{};
    report.append("REPORT/1\n");
    report.hex(pci, sizeof(pci));
    for (int i = 0; i < 16; ++i) report.append("disk0s1,size=2147483648,uuid=00000000-0000-0000-0000-000000000000\n");
    assert(!report.clipped && report.used < 2048);
}
