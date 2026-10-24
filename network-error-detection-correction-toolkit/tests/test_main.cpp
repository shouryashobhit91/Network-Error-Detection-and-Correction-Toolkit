// edc_tests - test cases for the toolkit under normal and failure conditions.
// Writes results/test_results.csv.
//
//   edc_tests            run all tests
//   edc_tests T07 T17    run selected tests
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <thread>

#include "codes.h"
#include "errors.h"
#include "link.h"
#include "sha256.h"

using namespace edc;

struct Out {
    bool pass;
    std::string detail;
};
#define CHECK(c, m) \
    do {            \
        if (!(c)) return Out{false, m}; \
    } while (0)

static std::mt19937 rng(99);
static Bytes randomBytes(size_t n) {
    Bytes b(n);
    for (auto &x : b) x = (std::uint8_t)rng();
    return b;
}

static Out t01() {
    Bytes s = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    std::string d;
    for (auto sp : {CRC8, CRC16, CRC32}) {
        CrcCodec c(sp);
        CHECK(c.compute(s) == sp.check, std::string(sp.name) + " check value wrong");
        char b[48];
        std::snprintf(b, sizeof(b), "%s=0x%X ", sp.id, (unsigned)sp.check);
        d += b;
    }
    return {true, "CRC of \"123456789\": " + d + "(standard check values)"};
}

static Out t02() {
    Bytes d = {0x00, 0x01, 0xF2, 0x03, 0xF4, 0xF5, 0xF6, 0xF7};   // RFC 1071 example
    CHECK(ChecksumCodec::sum(d) == 0xDDF2, "one's complement sum is not 0xDDF2");
    ChecksumCodec c;
    Bits cw = c.encode(d);
    CHECK(c.decode(cw, d.size()).status == Status::OK, "valid checksum rejected");
    return {true, "RFC 1071 example: sum 0xDDF2, checksum 0x220D, verified by the receiver"};
}

static Out t03() {
    int minD = 99;
    for (int a = 0; a < 16; a++)
        for (int b = a + 1; b < 16; b++) {
            std::uint8_t x[7], y[7];
            HammingCodec::encodeNibble(a, x);
            HammingCodec::encodeNibble(b, y);
            int d = 0;
            for (int k = 0; k < 7; k++) d += x[k] != y[k];
            minD = std::min(minD, d);
        }
    CHECK(minD == 3, "minimum distance is " + std::to_string(minD));
    return {true, "all 16 Hamming(7,4) codewords differ in at least 3 bits (dmin = 3)"};
}

static Out t04() {
    int runs = 0;
    for (auto &c : allCodecs())
        for (size_t n : {1, 2, 7, 64, 100, 1500}) {
            Bytes d = randomBytes(n);
            DecodeResult r = c->decode(c->encode(d), n);
            CHECK(r.status == Status::OK && r.data == d, c->name() + " false alarm at " + std::to_string(n) + " bytes");
            runs++;
        }
    return {true, std::to_string(runs) + " error-free frames (1 to 1500 bytes, all 9 codes): no false alarm"};
}

static Out t05() {
    Bytes d = randomBytes(32);
    std::string out;
    for (auto &c : allCodecs()) {
        if (c->corrects()) continue;
        Bits cw = c->encode(d);
        for (size_t i = 0; i < cw.size(); i++) {
            Bits x = cw;
            x[i] ^= 1;
            CHECK(c->decode(x, d.size()).status == Status::DETECTED, c->name() + " missed bit " + std::to_string(i));
        }
    }
    return {true, "every single-bit error in a 32-byte frame detected by parity, checksum, CRC-8/16/32"};
}

static Out t06() {
    Bytes d = randomBytes(32);
    int total = 0;
    for (auto &c : allCodecs()) {
        if (!c->corrects()) continue;
        Bits cw = c->encode(d);
        for (size_t i = 0; i < cw.size(); i++) {
            Bits x = cw;
            x[i] ^= 1;
            DecodeResult r = c->decode(x, d.size());
            CHECK(r.status == Status::CORRECTED && r.data == d, c->name() + " did not correct bit " + std::to_string(i));
            total++;
        }
    }
    return {true, std::to_string(total) + " single-bit errors all corrected by 2-D parity and the 4 Hamming variants"};
}

static Out t07() {
    long long pairs = 0;
    // CRC-8 (x^8+x^2+x+1) guarantees double-error detection only for frames shorter
    // than its period (127 bits), so it is tested with 8 data bytes (72-bit frame)
    for (auto sp : {CRC8, CRC16, CRC32}) {
        CrcCodec c(sp);
        Bytes d = randomBytes(sp.width == 8 ? 8 : 16);
        Bits cw = c.encode(d);
        for (size_t i = 0; i < cw.size(); i++)
            for (size_t j = i + 1; j < cw.size(); j++) {
                Bits x = cw;
                x[i] ^= 1;
                x[j] ^= 1;
                CHECK(c.decode(x, d.size()).status == Status::DETECTED, std::string(sp.name) + " missed a double error");
                pairs++;
            }
    }
    return {true, std::to_string(pairs) + " double-bit errors (every pair of positions) detected: CRC-8 on a 72-bit "
                  "frame, CRC-16/32 on 16-byte frames"};
}

static Out t08() {
    ParityCodec p;
    Bytes d = randomBytes(16);
    int missed = 0, tot = 0;
    for (int t = 0; t < 2000; t++) {
        Bits cw = p.encode(d);
        flipRandom(cw, 2, rng);
        tot++;
        if (p.decode(cw, d.size()).status != Status::DETECTED) missed++;
    }
    CHECK(missed == tot, "single parity unexpectedly detected an even number of errors");
    return {true, "known limit confirmed: single parity misses 100% of double-bit errors (2000/2000)"};
}

static Out t09() {
    HammingCodec s(true);
    Bytes d = randomBytes(8);
    Bits cw = s.encode(d);
    int n = 0;
    for (size_t b = 0; b < cw.size() / 8; b++)
        for (int i = 0; i < 8; i++)
            for (int j = i + 1; j < 8; j++) {
                Bits x = cw;
                x[b * 8 + i] ^= 1;
                x[b * 8 + j] ^= 1;
                CHECK(s.decode(x, d.size()).status == Status::DETECTED, "SEC-DED missed a double error");
                n++;
            }
    return {true, std::to_string(n) + " double errors inside one (8,4) block: all detected (not mis-corrected)"};
}

static Out t10() {
    ParityCodec p;
    int n = 0;
    for (int k : {1, 3, 5, 7, 9})
        for (int t = 0; t < 500; t++) {
            Bytes d = randomBytes(20);
            Bits cw = p.encode(d);
            flipRandom(cw, k, rng);
            CHECK(p.decode(cw, d.size()).status == Status::DETECTED, "odd error count missed");
            n++;
        }
    return {true, std::to_string(n) + " frames with 1, 3, 5, 7, 9 errors: all detected by single parity"};
}

static Out t11() {
    std::string d;
    for (auto sp : {CRC8, CRC16, CRC32}) {
        CrcCodec c(sp);
        for (int L = 1; L <= sp.width; L++)
            for (int t = 0; t < 300; t++) {
                Bytes data = randomBytes(64);
                Bits cw = c.encode(data);
                flipBurstRandom(cw, L, rng);
                CHECK(c.decode(cw, data.size()).status == Status::DETECTED,
                      std::string(sp.name) + " missed a burst of " + std::to_string(L));
            }
    }
    return {true, "all bursts up to the CRC width detected: CRC-8 <= 8, CRC-16 <= 16, CRC-32 <= 32 bits"};
}

static Out t12() {
    HammingCodec h(false, true);
    Bytes d = randomBytes(16);   // 32 codewords, so bursts up to 32 bits hit each codeword once
    for (int L = 2; L <= 32; L++)
        for (size_t start = 0; start + L <= h.codewordBits(d.size()); start += 7) {
            Bits cw = h.encode(d);
            std::vector<size_t> pos;
            for (int k = 0; k < L; k++) pos.push_back(start + (size_t)k);
            flipBits(cw, pos);   // worst case: every bit of the burst is wrong
            DecodeResult r = h.decode(cw, d.size());
            CHECK(r.data == d, "burst of " + std::to_string(L) + " not corrected");
        }
    return {true, "interleaved Hamming(7,4), 16-byte frame: every solid burst of 2..32 bits corrected"};
}

static Out t13() {
    HammingCodec h(false, false);
    int wrong = 0, n = 0;
    for (int t = 0; t < 1000; t++) {
        Bytes d = randomBytes(16);
        Bits cw = h.encode(d);
        size_t start = rng() % (cw.size() - 8);
        std::vector<size_t> pos;
        for (int k = 0; k < 8; k++) pos.push_back(start + (size_t)k);
        flipBits(cw, pos);
        n++;
        if (h.decode(cw, d.size()).data != d) wrong++;
    }
    CHECK(wrong == n, "plain Hamming corrected an 8-bit burst");
    return {true, "without interleaving an 8-bit solid burst is never corrected (" + std::to_string(wrong) + "/" +
                      std::to_string(n) + " wrong): shows why interleaving is used"};
}

static Out t14() {
    Parity2DCodec p;
    Bytes d = randomBytes(8);
    Bits cw = p.encode(d);
    flipBits(cw, {1 * 9 + 2, 1 * 9 + 5, 4 * 9 + 2, 4 * 9 + 5});   // corners of a rectangle
    DecodeResult r = p.decode(cw, d.size());
    CHECK(r.status == Status::OK && r.data != d, "rectangle pattern was noticed");
    return {true, "known limit confirmed: a 4-bit rectangle error pattern passes 2-D parity undetected"};
}

static Out t15() {
    // malformed or truncated codewords must not crash the decoders
    for (auto &c : allCodecs()) {
        Bytes d = randomBytes(10);
        Bits cw = c->encode(d);
        cw.resize(cw.size() / 2);
        c->decode(cw, d.size());
        c->decode(Bits(), d.size());
    }
    Frame f;
    CHECK(!parseFrame("", f) && !parseFrame("ED", f) && !parseFrame(std::string(40, 'x'), f), "garbage frame accepted");
    return {true, "truncated codewords and garbage frames handled without crashing"};
}

// ---------------------------------------------------------------- network
struct Net {
    Receiver rx;
    Channel ch;
    int port = 0;
    bool start(const ChannelConfig &c) {
        if (!rx.start(0)) return false;
        if (!ch.start(0, "127.0.0.1", rx.port(), c)) return false;
        port = ch.port();
        return true;
    }
};

static std::string file(size_t n) {
    std::mt19937 g(7);
    std::string s(n, '\0');
    for (auto &c : s) c = (char)g();
    return s;
}

static Out t16() {
    std::string d = file(8192);
    for (auto &c : allCodecs()) {
        Net n;
        ChannelConfig cc;
        CHECK(n.start(cc), "setup failed");
        SendOptions o;
        SendResult r = sendData("127.0.0.1", n.port, *c, d, o);
        CHECK(r.ok && r.fileMatch, c->name() + ": " + r.error);
        CHECK(r.nakRetx + r.timeoutRetx == 0, c->name() + " retransmitted on a clean channel");
    }
    return {true, "8 KB over a clean channel with each of the 9 codes: identical file, 0 retransmissions"};
}

static Out netRun(const std::string &id, const ChannelConfig &cc, size_t bytes, SendResult &r, ReceiverStats &rs) {
    Net n;
    CHECK(n.start(cc), "setup failed");
    SendOptions o;
    o.timeoutMs = 40;
    r = sendData("127.0.0.1", n.port, *codecById(id), file(bytes), o);
    n.rx.waitFinished(300);
    rs = n.rx.stats();
    return {true, ""};
}

static Out t17() {
    ChannelConfig cc;
    cc.ber = 1e-3;
    cc.seed = 3;
    SendResult r;
    ReceiverStats s;
    netRun("crc32", cc, 16384, r, s);
    CHECK(r.ok && r.fileMatch, "file damaged or transfer failed: " + r.error);
    CHECK(s.detected > 0, "no error detected at BER 1e-3");
    return {true, "BER 1e-3, CRC-32: " + std::to_string(s.detected) + " damaged frames detected and resent, file identical"};
}

static Out t18() {
    ChannelConfig cc;
    cc.ber = 1e-3;
    cc.seed = 3;
    cc.protectHeader = true;
    SendResult r;
    ReceiverStats s;
    netRun("hamming", cc, 16384, r, s);
    CHECK(r.ok && r.fileMatch, "file damaged: " + r.error);
    CHECK(s.corrected > 0 && s.detected == 0, "no forward correction happened");
    return {true, "BER 1e-3 (payload errors), Hamming(7,4): " + std::to_string(s.corrected) + " of " +
                      std::to_string(s.frames) + " frames corrected at the receiver, no data frame resent, file identical"};
}

static Out t19() {
    ChannelConfig cc;
    cc.ber = 2e-3;
    cc.seed = 5;
    SendResult r;
    ReceiverStats s;
    netRun("parity", cc, 16384, r, s);
    CHECK(r.ok, "transfer failed: " + r.error);
    CHECK(!r.fileMatch, "parity delivered a perfect file at BER 2e-3 (unexpected)");
    return {true, "BER 2e-3, single parity: frames with 2 errors slipped through; the end-to-end SHA-256 check "
                  "reported the file as CORRUPTED"};
}

static Out t20() {
    ChannelConfig cc;
    cc.burst = true;
    cc.pGoodToBad = 0.0005;
    cc.pBadToGood = 0.1;
    cc.seed = 8;
    SendResult r;
    ReceiverStats s;
    netRun("crc32", cc, 16384, r, s);
    CHECK(r.ok && r.fileMatch, "file damaged: " + r.error);
    return {true, "burst channel (Gilbert-Elliott), CRC-32: " + std::to_string(s.detected) +
                      " burst-damaged frames detected, file identical"};
}

static Out t21() {
    ChannelConfig cc;
    cc.drop = 0.2;
    cc.seed = 4;
    SendResult r;
    ReceiverStats s;
    netRun("crc16", cc, 8192, r, s);
    CHECK(r.ok && r.fileMatch, "transfer failed: " + r.error);
    CHECK(r.timeoutRetx > 0, "no timeout happened with 20% loss");
    return {true, "20% datagram loss: " + std::to_string(r.timeoutRetx) + " retransmissions after timeout, " +
                      std::to_string(s.duplicates) + " duplicates discarded, file identical"};
}

static Out t22() {
    ChannelConfig cc;
    cc.ber = 2e-3;
    cc.seed = 6;
    SendResult r;
    ReceiverStats s;
    netRun("crc32", cc, 8192, r, s);
    CHECK(r.ok && r.fileMatch, "transfer failed: " + r.error);
    CHECK(s.headerErrors > 0 && r.badReplies > 0, "no damaged headers seen");
    return {true, "BER 2e-3: " + std::to_string(s.headerErrors) + " data headers and " + std::to_string(r.badReplies) +
                      " ACK/NAK replies damaged, rejected by the header CRC-16; file identical"};
}

static Out t23() {
    sock_t s = net::udpSocket("127.0.0.1", 0);
    int port = net::localPort(s);
    CLOSE_SOCKET(s);
    SendOptions o;
    o.timeoutMs = 30;
    o.maxRetries = 10;
    double t = net::nowMs();
    SendResult r = sendData("127.0.0.1", port, *codecById("crc32"), file(1000), o);
    double el = net::nowMs() - t;
    CHECK(!r.ok, "transfer to nowhere succeeded");
    char b[160];
    std::snprintf(b, sizeof(b), "receiver not running: sender gave up after %.2f s (%s)", el / 1000, r.error.c_str());
    return {true, b};
}

static Out t24() {
    Receiver rx;
    CHECK(rx.start(0), "setup failed");
    sock_t s = net::udpSocket("127.0.0.1", 0);
    net::Addr to;
    net::resolve("127.0.0.1", rx.port(), to);
    std::string junk[] = {"x", std::string(15, 'E'), "ED garbage frame......", std::string(64, '\xff')};
    for (auto &j : junk) net::sendTo(s, j.data(), j.size(), to);
    CLOSE_SOCKET(s);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(rx.stats().headerErrors == 4 && rx.stats().frames == 0, "malformed datagrams not rejected");
    ChannelConfig cc;
    Channel ch;
    ch.start(0, "127.0.0.1", rx.port(), cc);
    SendOptions o;
    SendResult r = sendData("127.0.0.1", ch.port(), *codecById("checksum"), file(2000), o);
    CHECK(r.ok && r.fileMatch, "receiver broken after junk");
    return {true, "4 malformed datagrams rejected by the receiver, which then received a file normally"};
}

int main(int argc, char **argv) {
    net::start();
    struct T {
        const char *id, *name, *type;
        Out (*fn)();
    };
    std::vector<T> tests = {
        {"T01", "CRC-8/16/32 standard check values", "unit", t01},
        {"T02", "Internet checksum (RFC 1071 example)", "unit", t02},
        {"T03", "Hamming(7,4) minimum distance", "unit", t03},
        {"T04", "Error-free frames, all codes and sizes", "normal", t04},
        {"T05", "All single-bit errors detected", "failure", t05},
        {"T06", "All single-bit errors corrected", "failure", t06},
        {"T07", "All double-bit errors detected by CRC", "failure", t07},
        {"T08", "Single parity vs double-bit errors", "failure", t08},
        {"T09", "SEC-DED detects double errors", "failure", t09},
        {"T10", "Single parity vs odd error counts", "failure", t10},
        {"T11", "CRC burst detection up to width", "failure", t11},
        {"T12", "Interleaved Hamming corrects bursts", "failure", t12},
        {"T13", "Plain Hamming vs 8-bit burst", "failure", t13},
        {"T14", "2-D parity rectangle pattern", "failure", t14},
        {"T15", "Truncated codewords and garbage frames", "failure", t15},
        {"T16", "Network transfer, clean channel", "normal", t16},
        {"T17", "Network, BER 1e-3, CRC-32 + ARQ", "failure", t17},
        {"T18", "Network, BER 1e-3, Hamming FEC", "failure", t18},
        {"T19", "Network, BER 2e-3, single parity", "failure", t19},
        {"T20", "Network, burst errors, CRC-32", "failure", t20},
        {"T21", "Network, 20% datagram loss", "failure", t21},
        {"T22", "Network, damaged headers and ACKs", "failure", t22},
        {"T23", "Receiver not running", "failure", t23},
        {"T24", "Malformed datagrams at the receiver", "failure", t24},
    };
    std::vector<std::string> only(argv + 1, argv + argc);
    std::ofstream csv("results/test_results.csv");
    csv << "id,name,type,result,time_s,detail\n";
    int run = 0, pass = 0;
    double all = net::nowMs();
    std::printf("Error Detection and Correction Toolkit - test suite\n%s\n", std::string(78, '=').c_str());
    for (auto &t : tests) {
        if (!only.empty() && std::find(only.begin(), only.end(), t.id) == only.end()) continue;
        double t0 = net::nowMs();
        Out o = t.fn();
        double el = (net::nowMs() - t0) / 1000;
        run++;
        pass += o.pass;
        std::string head = std::string(t.id) + " " + t.name;
        std::printf("%s %s %s  %.2f s\n      %s\n", head.c_str(), std::string(head.size() < 52 ? 52 - head.size() : 1, '.').c_str(),
                    o.pass ? "PASS" : "FAIL", el, o.detail.c_str());
        std::fflush(stdout);
        std::string d = o.detail;
        std::replace(d.begin(), d.end(), '"', '\'');
        csv << t.id << ",\"" << t.name << "\"," << t.type << "," << (o.pass ? "PASS" : "FAIL") << "," << el << ",\"" << d << "\"\n";
    }
    std::printf("%s\n%d/%d tests passed in %.1f s\n", std::string(78, '=').c_str(), pass, run, (net::nowMs() - all) / 1000);
    net::stop();
    return pass == run ? 0 : 1;
}
