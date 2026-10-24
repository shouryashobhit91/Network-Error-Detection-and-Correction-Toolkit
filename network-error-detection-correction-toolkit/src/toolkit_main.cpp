// edc_toolkit - interactive Network Error Detection and Correction Toolkit.
//
// The user enters data, chooses a code (parity, 2-D parity, Internet checksum,
// CRC-8/16/32, Hamming (7,4), Hamming SEC-DED, Hamming with interleaving),
// introduces errors and sees whether the receiver detects or corrects them.
// The same error type can be applied to every code to compare them.
//
//   edc_toolkit                       interactive menu
//   edc_toolkit encode  --text HELLO --scheme crc32
//   edc_toolkit inject  --text HELLO --scheme hamming --flip 3,17
//   edc_toolkit compare --text HELLO --errors 2 | --burst 12 | --ber 0.01  [--trials 1000]
//   edc_toolkit crcdiv  --bits 11010011101100 --gen 1011
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>

#include "codes.h"
#include "errors.h"

#ifdef _WIN32
#include <io.h>
#define IS_TTY() _isatty(_fileno(stdin))
#else
#include <unistd.h>
#define IS_TTY() isatty(fileno(stdin))
#endif

using namespace edc;

static std::mt19937 rng(2026);

static std::map<std::string, std::string> parseArgs(int argc, char **argv, int from) {
    std::map<std::string, std::string> m;
    for (int i = from; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) m[a.substr(2)] = (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2)) ? argv[++i] : "1";
    }
    return m;
}

static std::string ask(const std::string &prompt) {
    std::cout << prompt << std::flush;
    std::string s;
    if (!std::getline(std::cin, s)) return "0";
    if (!IS_TTY()) std::cout << s << "\n";   // show the typed input when input is redirected
    return s;
}

static std::string printable(const Bytes &d) {
    std::string s;
    for (auto c : d) s += (c >= 32 && c < 127) ? (char)c : '.';
    return s;
}

static Bytes parseData(const std::string &in) {
    std::string t = in;
    if (t.rfind("hex:", 0) == 0) {   // hex:48 45 4C
        Bytes b;
        std::istringstream ss(t.substr(4));
        std::string h;
        while (ss >> h) b.push_back((std::uint8_t)std::stoi(h, nullptr, 16));
        return b;
    }
    if (t.rfind("bin:", 0) == 0) {   // bin:01001000 (padded to whole bytes)
        Bits bits;
        for (char c : t.substr(4))
            if (c == '0' || c == '1') bits.push_back((std::uint8_t)(c - '0'));
        return toBytes(bits);
    }
    return Bytes(t.begin(), t.end());
}

// codeword in groups of 8 bits, with ^ under the flipped bits
static void showCodeword(const std::string &label, const Bits &cw, const std::vector<size_t> &marks = {}) {
    const size_t perLine = 64;
    size_t shown = std::min<size_t>(cw.size(), 256);
    for (size_t start = 0; start < shown; start += perLine) {
        size_t end = std::min(shown, start + perLine);
        std::string bits, mark;
        for (size_t i = start; i < end; i++) {
            if (i > start && (i - start) % 8 == 0) {
                bits += ' ';
                mark += ' ';
            }
            bits += cw[i] ? '1' : '0';
            mark += std::find(marks.begin(), marks.end(), i) != marks.end() ? '^' : ' ';
        }
        std::printf("  %-12s %s\n", start == 0 ? label.c_str() : "", bits.c_str());
        if (!marks.empty() && mark.find('^') != std::string::npos) std::printf("  %-12s %s\n", "", mark.c_str());
    }
    if (cw.size() > shown) std::printf("  %-12s ... (%zu bits in total)\n", "", cw.size());
}

static void encodeView(const Codec &c, const Bytes &data) {
    Bits cw = c.encode(data);
    std::printf("\n  Scheme       %s\n", c.name().c_str());
    std::printf("  Data         \"%s\"  (%zu bytes = %zu bits)\n", printable(data).c_str(), data.size(), data.size() * 8);
    std::printf("  Hex          %s\n", hexString(data, 32).c_str());
    showCodeword("Codeword", cw);
    std::printf("  Length       %zu bits, redundancy %zu bits, overhead %.1f %%\n", cw.size(), cw.size() - data.size() * 8,
                c.overheadPercent(data.size()));
    if (auto crc = dynamic_cast<const CrcCodec *>(&c))
        std::printf("  CRC value    0x%0*X\n", crc->spec().width / 4, (unsigned)crc->compute(data));
    if (dynamic_cast<const ChecksumCodec *>(&c))
        std::printf("  Checksum     0x%04X\n", (unsigned)(std::uint16_t)~ChecksumCodec::sum(data));
}

// apply errors, decode and explain the outcome
static void injectView(const Codec &c, const Bytes &data, Bits cw, std::vector<size_t> flipped) {
    Bits sent = c.encode(data);
    std::printf("\n");
    showCodeword("Sent", sent);
    showCodeword("Received", cw, flipped);
    std::printf("  Errors       %zu bit(s) flipped", flipped.size());
    if (!flipped.empty()) {
        std::printf(" at position");
        for (size_t i = 0; i < flipped.size() && i < 12; i++) std::printf(" %zu", flipped[i]);
        if (flipped.size() > 12) std::printf(" ...");
    }
    std::printf("\n");
    DecodeResult r = c.decode(cw, data.size());
    std::printf("  Receiver     %s\n  Detail       %s\n", statusName(r.status), r.detail.c_str());
    std::printf("  Data out     \"%s\"\n", printable(r.data).c_str());
    const char *verdict;
    if (r.status == Status::DETECTED)
        verdict = "error detected: frame discarded, receiver asks for a retransmission (NAK)";
    else if (r.data == data)
        verdict = flipped.empty() ? "correct data delivered" : "correct data delivered (error corrected)";
    else
        verdict = "UNDETECTED ERROR: wrong data accepted as correct!";
    std::printf("  Result       %s\n", verdict);
}

static std::vector<size_t> applyErrors(Bits &cw, const std::string &kind, double value, const std::vector<size_t> &pos) {
    if (kind == "flip") return flipBits(cw, pos);
    if (kind == "random") return flipRandom(cw, (int)value, rng);
    if (kind == "burst") return flipBurstRandom(cw, (int)value, rng);
    return flipBer(cw, value, rng);
}

// every code, same kind of error; one example and statistics over many trials
static void compareView(const Bytes &data, const std::string &kind, double value, int trials) {
    std::string what = kind == "random" ? std::to_string((int)value) + " random bit error(s)"
                       : kind == "burst" ? "burst error of length " + std::to_string((int)value)
                                         : "bit error rate " + std::to_string(value);
    std::printf("\n  Comparison with %s, data \"%s\" (%zu bytes), %d trials per code\n\n", what.c_str(),
                printable(data).c_str(), data.size(), trials);
    std::printf("  %-31s %6s %8s | %-24s | %8s %9s %10s\n", "Code", "bits", "overhead", "one example", "detected",
                "corrected", "UNDETECTED");
    std::printf("  %s\n", std::string(111, '-').c_str());
    for (auto &c : allCodecs()) {
        int det = 0, cor = 0, und = 0, clean = 0;
        std::string example;
        for (int t = 0; t < trials; t++) {
            Bits cw = c->encode(data);
            auto f = applyErrors(cw, kind, value, {});
            DecodeResult r = c->decode(cw, data.size());
            std::string res;
            if (f.empty()) {
                clean++;
                res = "no error happened";
            } else if (r.status == Status::DETECTED) {
                det++;
                res = "detected";
            } else if (r.data == data) {
                cor++;
                res = "corrected";
            } else {
                und++;
                res = "UNDETECTED";
            }
            if (t == 0) example = std::to_string(f.size()) + " flipped -> " + res;
        }
        int hit = std::max(1, trials - clean);
        std::printf("  %-31s %6zu %7.1f%% | %-24s | %7.1f%% %8.1f%% %9.1f%%\n", c->name().c_str(),
                    c->codewordBits(data.size()), c->overheadPercent(data.size()), example.c_str(), 100.0 * det / hit,
                    100.0 * cor / hit, 100.0 * und / hit);
    }
    std::printf("\n  detected = receiver discards and asks again, corrected = right data without\n"
                "  retransmission, UNDETECTED = wrong data accepted (the dangerous case)\n");
}

static void crcDivView(const std::string &msg, const std::string &gen) {
    Bits m, g;
    for (char c : msg)
        if (c == '0' || c == '1') m.push_back((std::uint8_t)(c - '0'));
    for (char c : gen)
        if (c == '0' || c == '1') g.push_back((std::uint8_t)(c - '0'));
    if (m.empty() || g.size() < 2 || g[0] != 1) {
        std::printf("  give a binary message and a generator that starts with 1\n");
        return;
    }
    std::printf("\n  Sender: divide the message followed by %zu zeros by the generator %s\n", g.size() - 1,
                bitString(g, 0, SIZE_MAX, 0).c_str());
    auto steps = crcDivisionSteps(m, g);
    for (auto &line : steps) std::printf("  %s\n", line.c_str());
    std::string rem = steps.back().substr(steps.back().find(':') + 2);
    Bits frame = m;
    for (char c : rem) frame.push_back((std::uint8_t)(c - '0'));
    std::printf("  frame sent        : %s + %s\n", bitString(m, 0, SIZE_MAX, 0).c_str(), rem.c_str());
    std::printf("\n  Receiver: divide the received frame by the generator\n");
    auto check = crcDivisionSteps(frame, g, false);
    std::printf("  %s\n  %s  -> %s\n", check.front().c_str(), check.back().c_str(),
                check.back().find('1') == std::string::npos ? "remainder 0, no error detected" : "error detected");
    frame[frame.size() / 2] ^= 1;
    auto bad = crcDivisionSteps(frame, g, false);
    std::string badRem = bad.back().substr(bad.back().find(':') + 2);
    std::printf("  bit %zu flipped    : remainder %s -> %s\n", frame.size() / 2, badRem.c_str(),
                badRem.find('1') == std::string::npos ? "remainder 0 (not detected)" : "non-zero remainder, error detected");
}

static void interactive() {
    Bytes data = parseData("HELLO");
    auto codec = codecById("crc32");
    auto codecs = allCodecs();
    std::printf("====================================================================\n"
                "        Network Error Detection and Correction Toolkit (C++)\n"
                "====================================================================\n");
    for (;;) {
        std::printf("\n  Data: \"%s\" (%zu bytes)    Code: %s\n", printable(data).c_str(), data.size(), codec->name().c_str());
        std::printf("  1. Enter data            2. Choose code           3. Encode and show codeword\n"
                    "  4. Introduce errors      5. Compare all codes     6. CRC long division\n"
                    "  0. Exit\n");
        std::string ch = ask("  Choice: ");
        if (ch == "0" || ch == "q") break;
        if (ch == "1") {
            data = parseData(ask("  Data (text, or hex:48 49, or bin:01001000): "));
            if (data.empty()) data = parseData("HELLO");
        } else if (ch == "2") {
            for (size_t i = 0; i < codecs.size(); i++)
                std::printf("   %zu. %-34s overhead %5.1f %% for this data\n", i + 1, codecs[i]->name().c_str(),
                            codecs[i]->overheadPercent(data.size()));
            int k = std::atoi(ask("  Code number: ").c_str());
            if (k >= 1 && k <= (int)codecs.size()) codec = codecs[k - 1];
        } else if (ch == "3") {
            encodeView(*codec, data);
        } else if (ch == "4") {
            Bits cw = codec->encode(data);
            std::printf("  Codeword has %zu bits (positions 0 to %zu)\n", cw.size(), cw.size() - 1);
            std::printf("  a. flip chosen bits   b. random bits   c. burst   d. bit error rate\n");
            std::string t = ask("  Error type: ");
            std::vector<size_t> f;
            if (t == "a") f = flipBits(cw, parsePositions(ask("  Bit positions (e.g. 3,17): ")));
            else if (t == "b") f = flipRandom(cw, std::atoi(ask("  How many bits: ").c_str()), rng);
            else if (t == "c") f = flipBurstRandom(cw, std::atoi(ask("  Burst length: ").c_str()), rng);
            else f = flipBer(cw, std::atof(ask("  Bit error rate (e.g. 0.01): ").c_str()), rng);
            injectView(*codec, data, cw, f);
        } else if (ch == "5") {
            std::printf("  b. random bits   c. burst   d. bit error rate\n");
            std::string t = ask("  Error type: ");
            if (t == "c")
                compareView(data, "burst", std::atoi(ask("  Burst length: ").c_str()), 1000);
            else if (t == "d")
                compareView(data, "ber", std::atof(ask("  Bit error rate: ").c_str()), 1000);
            else
                compareView(data, "random", std::atoi(ask("  How many bits: ").c_str()), 1000);
        } else if (ch == "6") {
            std::string m = ask("  Message bits: ");
            crcDivView(m, ask("  Generator bits (e.g. 1011 for x^3+x+1): "));
        }
    }
    std::printf("  Bye\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        interactive();
        return 0;
    }
    std::string cmd = argv[1];
    auto a = parseArgs(argc, argv, 2);
    Bytes data = parseData(a.count("text") ? a["text"] : (a.count("hex") ? "hex:" + a["hex"] : "HELLO"));
    if (a.count("seed")) rng.seed((unsigned)std::stoul(a["seed"]));
    auto codec = codecById(a.count("scheme") ? a["scheme"] : "crc32");
    if (!codec && cmd != "compare" && cmd != "crcdiv") {
        std::printf("unknown scheme; use one of:");
        for (auto &c : allCodecs()) std::printf(" %s", c->id().c_str());
        std::printf("\n");
        return 1;
    }
    if (cmd == "encode") {
        encodeView(*codec, data);
    } else if (cmd == "inject") {
        Bits cw = codec->encode(data);
        std::vector<size_t> f;
        if (a.count("flip")) f = flipBits(cw, parsePositions(a["flip"]));
        else if (a.count("errors")) f = flipRandom(cw, std::stoi(a["errors"]), rng);
        else if (a.count("burst")) f = flipBurstRandom(cw, std::stoi(a["burst"]), rng);
        else if (a.count("ber")) f = flipBer(cw, std::stod(a["ber"]), rng);
        encodeView(*codec, data);
        injectView(*codec, data, cw, f);
    } else if (cmd == "compare") {
        int trials = a.count("trials") ? std::stoi(a["trials"]) : 1000;
        if (a.count("burst")) compareView(data, "burst", std::stoi(a["burst"]), trials);
        else if (a.count("ber")) compareView(data, "ber", std::stod(a["ber"]), trials);
        else compareView(data, "random", a.count("errors") ? std::stoi(a["errors"]) : 1, trials);
    } else if (cmd == "crcdiv") {
        crcDivView(a.count("bits") ? a["bits"] : "11010011101100", a.count("gen") ? a["gen"] : "1011");
    } else {
        std::printf("commands: encode, inject, compare, crcdiv (or no command for the menu)\n");
        return 1;
    }
    return 0;
}
