// codes.cpp - parity, 2-D parity, Internet checksum, CRC-8/16/32 and Hamming codes.
#include "codes.h"

#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace edc {

// --------------------------------------------------------------- bit helpers
Bits toBits(const Bytes &data) {
    Bits b;
    b.reserve(data.size() * 8);
    for (std::uint8_t byte : data)
        for (int k = 7; k >= 0; k--) b.push_back((byte >> k) & 1);
    return b;
}

Bytes toBytes(const Bits &bits) {
    Bytes out((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); i++)
        if (bits[i]) out[i / 8] |= (std::uint8_t)(0x80 >> (i % 8));
    return out;
}

std::string bitString(const Bits &bits, size_t from, size_t count, int group) {
    std::string s;
    size_t end = count == SIZE_MAX ? bits.size() : std::min(bits.size(), from + count);
    for (size_t i = from; i < end; i++) {
        if (i > from && group > 0 && (i - from) % group == 0) s += ' ';
        s += bits[i] ? '1' : '0';
    }
    return s;
}

std::string hexString(const Bytes &data, size_t max) {
    std::string s;
    char b[4];
    for (size_t i = 0; i < data.size() && i < max; i++) {
        std::snprintf(b, sizeof(b), "%02X", data[i]);
        if (i) s += ' ';
        s += b;
    }
    if (data.size() > max) s += " ...";
    return s;
}

const char *statusName(Status s) {
    switch (s) {
        case Status::OK: return "NO ERROR DETECTED";
        case Status::CORRECTED: return "ERROR CORRECTED";
        case Status::DETECTED: return "ERROR DETECTED";
    }
    return "?";
}

static Bytes dataPart(const Bits &cw, size_t n) {
    Bits d(cw.begin(), cw.begin() + (long)std::min(cw.size(), n * 8));
    d.resize(n * 8, 0);
    return toBytes(d);
}

// ------------------------------------------------------------------ parity
Bits ParityCodec::encode(const Bytes &data) const {
    Bits b = toBits(data);
    int p = odd_ ? 1 : 0;
    for (auto x : b) p ^= x;
    b.push_back((std::uint8_t)p);   // total number of 1s becomes even (or odd)
    return b;
}

DecodeResult ParityCodec::decode(const Bits &cw, size_t n) const {
    DecodeResult r;
    r.data = dataPart(cw, n);
    int p = 0;
    for (auto x : cw) p ^= x;
    bool good = p == (odd_ ? 1 : 0);
    r.status = good ? Status::OK : Status::DETECTED;
    r.detail = std::string("number of 1s is ") + ((p == 0) ? "even" : "odd");
    return r;
}

// ---------------------------------------------------------------- checksum
std::uint16_t ChecksumCodec::sum(const Bytes &data) {
    std::uint32_t s = 0;
    for (size_t i = 0; i < data.size(); i += 2) {
        std::uint16_t word = (std::uint16_t)(data[i] << 8);
        if (i + 1 < data.size()) word |= data[i + 1];   // odd length: pad with a zero byte
        s += word;
        s = (s & 0xFFFF) + (s >> 16);                    // end-around carry
    }
    return (std::uint16_t)s;
}

Bits ChecksumCodec::encode(const Bytes &data) const {
    std::uint16_t c = (std::uint16_t)~sum(data);
    Bytes all = data;
    all.push_back((std::uint8_t)(c >> 8));
    all.push_back((std::uint8_t)(c & 0xFF));
    return toBits(all);
}

DecodeResult ChecksumCodec::decode(const Bits &cw, size_t n) const {
    DecodeResult r;
    r.data = dataPart(cw, n);
    Bits rest;
    if (cw.size() > n * 8) rest.assign(cw.begin() + (long)(n * 8), cw.end());
    Bytes tail = toBytes(rest);
    std::uint16_t got = (std::uint16_t)((tail.size() > 0 ? tail[0] << 8 : 0) | (tail.size() > 1 ? tail[1] : 0));
    std::uint32_t s = (std::uint32_t)sum(r.data) + got;
    s = (s & 0xFFFF) + (s >> 16);
    r.status = s == 0xFFFF ? Status::OK : Status::DETECTED;
    char b[64];
    std::snprintf(b, sizeof(b), "sum of data + checksum = 0x%04X (must be 0xFFFF)", (unsigned)s);
    r.detail = b;
    return r;
}

// --------------------------------------------------------------------- CRC
const CrcSpec CRC8 = {"crc8", "CRC-8 (x^8+x^2+x+1)", 8, 0x07, 0x00, 0x00, false, 0xF4, 5};
const CrcSpec CRC16 = {"crc16", "CRC-16-CCITT", 16, 0x1021, 0xFFFF, 0x0000, false, 0x29B1, 6};
const CrcSpec CRC32 = {"crc32", "CRC-32 (IEEE 802.3)", 32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, true, 0xCBF43926, 7};

static std::uint32_t reflectBits(std::uint32_t v, int width) {
    std::uint32_t r = 0;
    for (int i = 0; i < width; i++)
        if (v & (1u << i)) r |= 1u << (width - 1 - i);
    return r;
}

CrcCodec::CrcCodec(const CrcSpec &s) : s_(s) {
    std::uint32_t mask = s_.width == 32 ? 0xFFFFFFFFu : ((1u << s_.width) - 1);
    if (s_.reflect) {
        std::uint32_t rp = reflectBits(s_.poly, s_.width);
        for (std::uint32_t i = 0; i < 256; i++) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ rp : c >> 1;
            table_[i] = c;
        }
    } else {
        std::uint32_t top = 1u << (s_.width - 1);
        for (std::uint32_t i = 0; i < 256; i++) {
            std::uint32_t c = i << (s_.width - 8);
            for (int k = 0; k < 8; k++) c = (c & top) ? (c << 1) ^ s_.poly : c << 1;
            table_[i] = c & mask;
        }
    }
}

std::uint32_t CrcCodec::compute(const Bytes &data) const {
    std::uint32_t mask = s_.width == 32 ? 0xFFFFFFFFu : ((1u << s_.width) - 1);
    std::uint32_t crc = s_.init;
    if (s_.reflect) {
        for (std::uint8_t b : data) crc = table_[(crc ^ b) & 0xFF] ^ (crc >> 8);
    } else {
        for (std::uint8_t b : data) crc = ((crc << 8) ^ table_[((crc >> (s_.width - 8)) ^ b) & 0xFF]) & mask;
    }
    return (crc ^ s_.xorOut) & mask;
}

Bits CrcCodec::encode(const Bytes &data) const {
    Bits b = toBits(data);
    std::uint32_t c = compute(data);
    for (int k = s_.width - 1; k >= 0; k--) b.push_back((c >> k) & 1);   // append the remainder
    return b;
}

DecodeResult CrcCodec::decode(const Bits &cw, size_t n) const {
    DecodeResult r;
    r.data = dataPart(cw, n);
    std::uint32_t got = 0;
    for (int k = 0; k < s_.width; k++) {
        size_t i = n * 8 + k;
        got = (got << 1) | (i < cw.size() ? cw[i] : 0);
    }
    std::uint32_t want = compute(r.data);
    r.status = got == want ? Status::OK : Status::DETECTED;
    char b[96];
    std::snprintf(b, sizeof(b), "received CRC 0x%0*X, recomputed 0x%0*X", s_.width / 4, (unsigned)got, s_.width / 4,
                  (unsigned)want);
    r.detail = b;
    return r;
}

// ------------------------------------------------------------- 2-D parity
Bits Parity2DCodec::encode(const Bytes &data) const {
    Bits out;
    int col[9] = {0};
    for (std::uint8_t byte : data) {
        int rp = 0;
        for (int k = 7; k >= 0; k--) {
            int bit = (byte >> k) & 1;
            out.push_back((std::uint8_t)bit);
            rp ^= bit;
            col[7 - k] ^= bit;
        }
        out.push_back((std::uint8_t)rp);   // row parity
        col[8] ^= rp;
    }
    for (int j = 0; j < 9; j++) out.push_back((std::uint8_t)col[j]);  // parity row
    return out;
}

DecodeResult Parity2DCodec::decode(const Bits &cw, size_t n) const {
    Bits c = cw;
    c.resize(9 * n + 9, 0);
    std::vector<size_t> badRows, badCols;
    for (size_t i = 0; i < n; i++) {
        int p = 0;
        for (int j = 0; j < 9; j++) p ^= c[i * 9 + j];
        if (p) badRows.push_back(i);
    }
    for (int j = 0; j < 9; j++) {
        int p = 0;
        for (size_t i = 0; i <= n; i++) p ^= c[i * 9 + j];
        if (p) badCols.push_back((size_t)j);
    }
    DecodeResult r;
    std::ostringstream d;
    d << "failing rows " << badRows.size() << ", failing columns " << badCols.size();
    if (badRows.empty() && badCols.empty()) {
        r.status = Status::OK;
    } else if (badRows.size() == 1 && badCols.size() == 1) {
        size_t pos = badRows[0] * 9 + badCols[0];
        c[pos] ^= 1;                             // the bit at the crossing is wrong
        r.status = Status::CORRECTED;
        r.correctedBits = 1;
        d << " -> flipped row " << badRows[0] << ", column " << badCols[0];
    } else if (badRows.empty() && badCols.size() == 1) {
        r.status = Status::CORRECTED;            // only the parity row was hit, data is fine
        r.correctedBits = 1;
        d << " -> error in the parity row, data unchanged";
    } else {
        r.status = Status::DETECTED;
    }
    Bits data;
    for (size_t i = 0; i < n; i++)
        for (int j = 0; j < 8; j++) data.push_back(c[i * 9 + j]);
    r.data = toBytes(data);
    r.detail = d.str();
    return r;
}

// ------------------------------------------------------------------ Hamming
std::string HammingCodec::id() const {
    return std::string(secded_ ? "secded" : "hamming") + (inter_ ? "-il" : "");
}

std::string HammingCodec::name() const {
    return std::string(secded_ ? "Hamming SEC-DED (8,4)" : "Hamming (7,4)") + (inter_ ? " + interleaving" : "");
}

// positions 1..7 = p1 p2 d1 p3 d2 d3 d4
void HammingCodec::encodeNibble(int v, std::uint8_t c[7]) {
    int d1 = (v >> 3) & 1, d2 = (v >> 2) & 1, d3 = (v >> 1) & 1, d4 = v & 1;
    c[0] = (std::uint8_t)(d1 ^ d2 ^ d4);
    c[1] = (std::uint8_t)(d1 ^ d3 ^ d4);
    c[2] = (std::uint8_t)d1;
    c[3] = (std::uint8_t)(d2 ^ d3 ^ d4);
    c[4] = (std::uint8_t)d2;
    c[5] = (std::uint8_t)d3;
    c[6] = (std::uint8_t)d4;
}

Bits HammingCodec::encode(const Bytes &data) const {
    const int L = secded_ ? 8 : 7;
    std::vector<std::uint8_t> blocks;   // block after block
    for (std::uint8_t byte : data)
        for (int nib : {byte >> 4, byte & 15}) {
            std::uint8_t c[7];
            encodeNibble(nib, c);
            int all = 0;
            for (int k = 0; k < 7; k++) {
                blocks.push_back(c[k]);
                all ^= c[k];
            }
            if (secded_) blocks.push_back((std::uint8_t)all);   // overall parity bit
        }
    if (!inter_) return blocks;
    // block interleaver: write the codewords as rows, send the columns, so that a
    // burst hits each codeword at most once (when burst length <= number of rows)
    size_t rows = blocks.size() / L;
    Bits out;
    out.reserve(blocks.size());
    for (int j = 0; j < L; j++)
        for (size_t i = 0; i < rows; i++) out.push_back(blocks[i * L + j]);
    return out;
}

DecodeResult HammingCodec::decode(const Bits &cwIn, size_t n) const {
    const int L = secded_ ? 8 : 7;
    size_t rows = n * 2;
    Bits cw = cwIn;
    cw.resize(rows * L, 0);
    Bits blocks(rows * L);
    if (inter_) {
        size_t k = 0;
        for (int j = 0; j < L; j++)
            for (size_t i = 0; i < rows; i++) blocks[i * L + j] = cw[k++];
    } else {
        blocks = cw;
    }
    DecodeResult r;
    int corrected = 0, doubles = 0;
    std::vector<int> nibbles;
    std::ostringstream d;
    int shown = 0;
    for (size_t b = 0; b < rows; b++) {
        std::uint8_t *c = &blocks[b * L];
        int s = (c[0] ^ c[2] ^ c[4] ^ c[6]) | ((c[1] ^ c[2] ^ c[5] ^ c[6]) << 1) | ((c[3] ^ c[4] ^ c[5] ^ c[6]) << 2);
        if (secded_) {
            int all = 0;
            for (int k = 0; k < 8; k++) all ^= c[k];
            if (s != 0 && all == 0) {        // two errors: detected, not correctable
                doubles++;
                if (shown++ < 3) d << "block " << b << ": syndrome " << s << ", overall parity ok -> double error; ";
            } else if (all == 1) {           // one error (position s, or the overall bit if s == 0)
                if (s) c[s - 1] ^= 1;
                corrected++;
                if (shown++ < 3) d << "block " << b << ": syndrome " << s << " -> bit " << (s ? s : 8) << " corrected; ";
            }
        } else if (s) {
            c[s - 1] ^= 1;                   // Hamming(7,4) always assumes a single error
            corrected++;
            if (shown++ < 3) d << "block " << b << ": syndrome " << s << " -> bit " << s << " corrected; ";
        }
        nibbles.push_back((c[2] << 3) | (c[4] << 2) | (c[5] << 1) | c[6]);
    }
    r.data.resize(n);
    for (size_t i = 0; i < n; i++) r.data[i] = (std::uint8_t)((nibbles[2 * i] << 4) | nibbles[2 * i + 1]);
    r.correctedBits = corrected;
    r.status = doubles ? Status::DETECTED : (corrected ? Status::CORRECTED : Status::OK);
    if (shown > 3) d << "...";
    r.detail = shown ? d.str() : "all syndromes are 0";
    return r;
}

// ------------------------------------------------------------------- lists
std::vector<std::shared_ptr<Codec>> allCodecs() {
    return {std::make_shared<ParityCodec>(false),       std::make_shared<Parity2DCodec>(),
            std::make_shared<ChecksumCodec>(),          std::make_shared<CrcCodec>(CRC8),
            std::make_shared<CrcCodec>(CRC16),          std::make_shared<CrcCodec>(CRC32),
            std::make_shared<HammingCodec>(false),      std::make_shared<HammingCodec>(true),
            std::make_shared<HammingCodec>(false, true)};
}

std::shared_ptr<Codec> codecById(const std::string &id) {
    if (id == "parity-odd") return std::make_shared<ParityCodec>(true);
    if (id == "secded-il") return std::make_shared<HammingCodec>(true, true);
    for (auto &c : allCodecs())
        if (c->id() == id) return c;
    return nullptr;
}

std::shared_ptr<Codec> codecByWire(std::uint8_t w) {
    if (w == 2) return std::make_shared<ParityCodec>(true);
    if (w == 11) return std::make_shared<HammingCodec>(true, true);
    for (auto &c : allCodecs())
        if (c->wireId() == w) return c;
    return nullptr;
}

std::vector<std::string> crcDivisionSteps(const Bits &msg, const Bits &gen, bool appendZeros) {
    std::vector<std::string> out;
    size_t r = gen.size() - 1;
    Bits work = msg;
    if (appendZeros) work.insert(work.end(), r, 0);
    auto str = [](const Bits &b) { return bitString(b, 0, SIZE_MAX, 0); };
    out.push_back(std::string(appendZeros ? "dividend (+zeros)" : "received frame   ") + " : " + str(work));
    for (size_t i = 0; i + r < work.size(); i++) {
        if (!work[i]) continue;
        for (size_t k = 0; k < gen.size(); k++) work[i + k] ^= gen[k];
        char lab[32];
        std::snprintf(lab, sizeof(lab), "XOR at bit %-6zu : ", i);
        out.push_back(lab + std::string(i, ' ') + str(gen));
        out.push_back("                  = " + str(work));
    }
    out.push_back("remainder         : " + str(Bits(work.end() - (long)r, work.end())));
    return out;
}

}  // namespace edc
