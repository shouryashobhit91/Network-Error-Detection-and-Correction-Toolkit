// pcap.h - writes UDP datagrams into a .pcap file (LINKTYPE_RAW, IPv4) that
// Wireshark can open. Addresses in the capture are synthetic (10.0.0.x).
#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace edc {

class PcapWriter {
public:
    bool open(const std::string &path);
    void write(uint32_t srcIp, int srcPort, uint32_t dstIp, int dstPort, const std::string &payload);
    void close();
    ~PcapWriter() { close(); }

private:
    FILE *f_ = nullptr;
    uint16_t ipId_ = 1;
};

inline bool PcapWriter::open(const std::string &path) {
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) return false;
    uint32_t magic = 0xa1b2c3d4, snap = 65535, link = 101;  // 101 = LINKTYPE_RAW (IPv4)
    uint16_t vmaj = 2, vmin = 4;
    int32_t zone = 0;
    uint32_t sig = 0;
    std::fwrite(&magic, 4, 1, f_);
    std::fwrite(&vmaj, 2, 1, f_);
    std::fwrite(&vmin, 2, 1, f_);
    std::fwrite(&zone, 4, 1, f_);
    std::fwrite(&sig, 4, 1, f_);
    std::fwrite(&snap, 4, 1, f_);
    std::fwrite(&link, 4, 1, f_);
    return true;
}

// wraps the UDP payload in IPv4 + UDP headers so Wireshark shows normal addresses
inline void PcapWriter::write(uint32_t sip, int sport, uint32_t dip, int dport, const std::string &pl) {
    if (!f_) return;
    std::string pkt;
    uint16_t total = (uint16_t)(20 + 8 + pl.size());
    uint8_t ip[20] = {0x45, 0, (uint8_t)(total >> 8), (uint8_t)total, (uint8_t)(ipId_ >> 8),
                      (uint8_t)ipId_, 0x40, 0, 64, 17, 0, 0,
                      (uint8_t)(sip >> 24), (uint8_t)(sip >> 16), (uint8_t)(sip >> 8), (uint8_t)sip,
                      (uint8_t)(dip >> 24), (uint8_t)(dip >> 16), (uint8_t)(dip >> 8), (uint8_t)dip};
    ipId_++;
    uint32_t sum = 0;
    for (int i = 0; i < 20; i += 2) sum += (uint32_t)(ip[i] << 8 | ip[i + 1]);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t cs = (uint16_t)~sum;
    ip[10] = (uint8_t)(cs >> 8);
    ip[11] = (uint8_t)cs;
    uint16_t ulen = (uint16_t)(8 + pl.size());
    uint8_t udp[8] = {(uint8_t)(sport >> 8), (uint8_t)sport, (uint8_t)(dport >> 8), (uint8_t)dport,
                      (uint8_t)(ulen >> 8), (uint8_t)ulen, 0, 0};  // UDP checksum 0 = not used
    pkt.append((const char *)ip, 20);
    pkt.append((const char *)udp, 8);
    pkt += pl;
    auto now = std::chrono::system_clock::now().time_since_epoch();
    long long us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    uint32_t hdr[4] = {(uint32_t)(us / 1000000), (uint32_t)(us % 1000000), (uint32_t)pkt.size(),
                       (uint32_t)pkt.size()};
    std::fwrite(hdr, 4, 4, f_);
    std::fwrite(pkt.data(), 1, pkt.size(), f_);
}

inline void PcapWriter::close() {
    if (f_) std::fclose(f_);
    f_ = nullptr;
}


}  // namespace edc
