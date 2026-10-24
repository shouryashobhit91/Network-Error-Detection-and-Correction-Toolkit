// codes.h - error detection and correction codes used by the toolkit.
//
// Every code works on a block of data bytes and produces a codeword made of bits
// (one std::uint8_t per bit, value 0 or 1, most significant bit of each byte first).
// The receiver side decodes a (possibly damaged) codeword and reports whether the
// block looked correct, was corrected, or contained a detected error.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace edc {

using Bytes = std::vector<std::uint8_t>;
using Bits = std::vector<std::uint8_t>;  // each element is 0 or 1

Bits toBits(const Bytes &data);
Bytes toBytes(const Bits &bits);           // pads the last byte with zeros
std::string bitString(const Bits &bits, size_t from = 0, size_t count = SIZE_MAX, int group = 8);
std::string hexString(const Bytes &data, size_t max = 64);

enum class Status { OK, CORRECTED, DETECTED };
const char *statusName(Status s);

struct DecodeResult {
    Status status = Status::OK;
    Bytes data;          // recovered data (best effort when DETECTED)
    int correctedBits = 0;
    std::string detail;  // e.g. syndrome or failing row/column
};

class Codec {
public:
    virtual ~Codec() = default;
    virtual std::string id() const = 0;          // short name used on the command line
    virtual std::string name() const = 0;        // readable name
    virtual std::uint8_t wireId() const = 0;     // scheme number in the frame header
    virtual bool corrects() const = 0;           // can it correct errors?
    virtual Bits encode(const Bytes &data) const = 0;
    virtual DecodeResult decode(const Bits &codeword, size_t dataBytes) const = 0;
    size_t codewordBits(size_t dataBytes) const { return encode(Bytes(dataBytes, 0)).size(); }
    double overheadPercent(size_t dataBytes) const {
        return 100.0 * (codewordBits(dataBytes) - 8.0 * dataBytes) / (8.0 * dataBytes);
    }
};

// ---- detection only ------------------------------------------------------
class ParityCodec : public Codec {   // one even (or odd) parity bit for the whole block
public:
    explicit ParityCodec(bool odd = false) : odd_(odd) {}
    std::string id() const override { return odd_ ? "parity-odd" : "parity"; }
    std::string name() const override { return odd_ ? "Single parity (odd)" : "Single parity (even)"; }
    std::uint8_t wireId() const override { return odd_ ? 2 : 1; }
    bool corrects() const override { return false; }
    Bits encode(const Bytes &data) const override;
    DecodeResult decode(const Bits &cw, size_t n) const override;

private:
    bool odd_;
};

class ChecksumCodec : public Codec { // 16-bit Internet checksum (RFC 1071)
public:
    std::string id() const override { return "checksum"; }
    std::string name() const override { return "Internet checksum (16-bit)"; }
    std::uint8_t wireId() const override { return 4; }
    bool corrects() const override { return false; }
    Bits encode(const Bytes &data) const override;
    DecodeResult decode(const Bits &cw, size_t n) const override;
    static std::uint16_t sum(const Bytes &data);
};

struct CrcSpec {
    const char *id, *name;
    int width;
    std::uint32_t poly, init, xorOut;
    bool reflect;
    std::uint32_t check;  // CRC of the ASCII string "123456789"
    std::uint8_t wire;
};
extern const CrcSpec CRC8, CRC16, CRC32;

class CrcCodec : public Codec {
public:
    explicit CrcCodec(const CrcSpec &s);
    std::string id() const override { return s_.id; }
    std::string name() const override { return s_.name; }
    std::uint8_t wireId() const override { return s_.wire; }
    bool corrects() const override { return false; }
    Bits encode(const Bytes &data) const override;
    DecodeResult decode(const Bits &cw, size_t n) const override;
    std::uint32_t compute(const Bytes &data) const;
    const CrcSpec &spec() const { return s_; }

private:
    CrcSpec s_;
    std::uint32_t table_[256];
};

// ---- detection and correction --------------------------------------------
class Parity2DCodec : public Codec { // each byte is a row with a parity bit, plus a parity row
public:
    std::string id() const override { return "parity2d"; }
    std::string name() const override { return "Two-dimensional parity"; }
    std::uint8_t wireId() const override { return 3; }
    bool corrects() const override { return true; }
    Bits encode(const Bytes &data) const override;
    DecodeResult decode(const Bits &cw, size_t n) const override;
};

class HammingCodec : public Codec {  // Hamming(7,4) per nibble, optional SEC-DED (8,4)
public:
    explicit HammingCodec(bool secded = false, bool interleave = false) : secded_(secded), inter_(interleave) {}
    std::string id() const override;
    std::string name() const override;
    std::uint8_t wireId() const override { return secded_ ? (inter_ ? 11 : 9) : (inter_ ? 10 : 8); }
    bool corrects() const override { return true; }
    Bits encode(const Bytes &data) const override;
    DecodeResult decode(const Bits &cw, size_t n) const override;
    static void encodeNibble(int nibble, std::uint8_t out[7]);

private:
    bool secded_, inter_;
};

// all codecs offered by the toolkit, in a fixed order
std::vector<std::shared_ptr<Codec>> allCodecs();
std::shared_ptr<Codec> codecById(const std::string &id);
std::shared_ptr<Codec> codecByWire(std::uint8_t wire);

// step-by-step CRC long division (modulo-2) for teaching, small inputs only
std::vector<std::string> crcDivisionSteps(const Bits &message, const Bits &generator, bool appendZeros = true);

}  // namespace edc
