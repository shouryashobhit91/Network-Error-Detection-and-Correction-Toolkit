// link.h - frame format and the three network roles of the toolkit:
//
//   sender  --UDP-->  noisy channel  --UDP-->  receiver
//           <--ACK/NAK--             <--ACK/NAK--
//
// The sender splits a file into frames, protects each frame with the chosen
// code and uses stop-and-wait ARQ. The channel flips bits (random BER or bursts),
// can drop and delay datagrams and can write a pcap capture. The receiver decodes
// every frame, corrects what the code can correct, asks for a retransmission (NAK)
// when an error is detected, and checks the whole file with SHA-256 at the end.
//
// Frame header (16 bytes, big endian):
//   0-1  magic 0x4544 ("ED")      2  version (1)      3  type
//   4    scheme (code id)         5  flags            6-9 sequence number
//   10-11 data length in bytes    12-13 codeword length in bits
//   14-15 CRC-16-CCITT of bytes 0..13 (header check)
// followed by the codeword packed into bytes.
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "codes.h"
#include "errors.h"
#include "net_compat.h"
#include "pcap.h"

namespace edc {

const std::uint16_t FRAME_MAGIC = 0x4544;
const int FRAME_HEADER = 16;
enum FrameType : std::uint8_t { T_DATA = 1, T_ACK = 2, T_NAK = 3, T_FIN = 4, T_FIN_ACK = 5 };
const char *frameTypeName(std::uint8_t t);

struct Frame {
    std::uint8_t type = T_DATA, scheme = 0, flags = 0;
    std::uint32_t seq = 0;
    std::uint16_t dataLen = 0, codeBits = 0;
    Bytes payload;   // packed codeword
};

std::string buildFrame(const Frame &f);
// returns false when the datagram is too short, has the wrong magic or a bad header CRC
bool parseFrame(const std::string &d, Frame &f, bool *headerCrcBad = nullptr);

// ------------------------------------------------------------------ receiver
struct ReceiverStats {
    std::uint64_t frames = 0, clean = 0, corrected = 0, correctedBits = 0, detected = 0;
    std::uint64_t headerErrors = 0, duplicates = 0, bytes = 0;
    bool finished = false, fileMatch = false;
    std::string scheme, sha;
};

class Receiver {
public:
    bool start(int port, const std::string &outPath = "", bool verbose = false);
    void stop();
    ~Receiver() { stop(); }
    int port() const { return port_; }
    ReceiverStats stats();
    std::map<std::uint32_t, Bytes> frames();   // delivered data per sequence number
    std::string data();
    bool waitFinished(int ms);

private:
    void loop();
    void reply(std::uint8_t type, std::uint32_t seq, std::uint8_t flags, const net::Addr &to);
    sock_t s_ = BAD_SOCKET;
    int port_ = 0;
    std::string out_;
    bool verbose_ = false;
    std::atomic<bool> stop_{false};
    std::thread th_;
    std::mutex mu_;
    ReceiverStats st_;
    std::uint32_t expected_ = 0;
    std::string data_;
    std::map<std::uint32_t, Bytes> frames_;
};

// ------------------------------------------------------------------- channel
struct ChannelConfig {
    double ber = 0;                 // random bit error rate (each bit independently)
    bool burst = false;             // use the Gilbert-Elliott burst model instead
    double pGoodToBad = 0.0005, pBadToGood = 0.1, berBad = 0.5;
    double drop = 0;                // probability that a datagram is lost
    double delayMs = 0;             // one-way delay
    bool protectHeader = false;     // corrupt only the payload (used by some tests)
    unsigned seed = 1;
    std::string pcap;
    bool verbose = false;
};

struct ChannelStats {
    std::uint64_t datagrams = 0, corrupted = 0, bitsFlipped = 0, dropped = 0, bits = 0;
};

class Channel {
public:
    bool start(int listenPort, const std::string &host, int serverPort, const ChannelConfig &cfg);
    void stop();
    ~Channel() { stop(); }
    int port() const { return port_; }
    ChannelStats stats();
    void setConfig(const ChannelConfig &c);

private:
    void loop();
    std::string damage(const std::string &d, int dir);
    struct Item {
        double t;
        std::uint64_t n;
        sock_t via;
        net::Addr to;
        std::string data;
        int dir;
        bool operator>(const Item &o) const { return t != o.t ? t > o.t : n > o.n; }
    };
    ChannelConfig cfg_;
    sock_t main_ = BAD_SOCKET, up_ = BAD_SOCKET;
    net::Addr server_, client_;
    bool haveClient_ = false;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread th_;
    std::mutex mu_;
    std::mt19937 rng_;
    BurstChannel gilbert_[2];
    ChannelStats st_;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q_;
    std::uint64_t n_ = 0;
    PcapWriter pcap_;
};

// -------------------------------------------------------------------- sender
struct SendResult {
    bool ok = false, fileMatch = false;
    std::string error;
    std::uint64_t frames = 0, sent = 0, nakRetx = 0, timeoutRetx = 0, badReplies = 0;
    double seconds = 0, goodputKbps = 0, avgFrameMs = 0, maxFrameMs = 0, efficiency = 0;
    std::vector<double> frameMs;
};

struct SendOptions {
    int frameBytes = 64;
    int timeoutMs = 60;
    int maxRetries = 50;
    bool progress = false;
};

SendResult sendData(const std::string &host, int port, const Codec &codec, const std::string &data,
                    const SendOptions &opt);

}  // namespace edc
