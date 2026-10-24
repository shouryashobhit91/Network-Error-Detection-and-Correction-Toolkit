// link.cpp - frames, receiver, noisy channel and stop-and-wait ARQ sender.
#include "link.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>

#include "sha256.h"

namespace edc {

static CrcCodec &headerCrc() {
    static CrcCodec c(CRC16);
    return c;
}

const char *frameTypeName(std::uint8_t t) {
    switch (t) {
        case T_DATA: return "DATA";
        case T_ACK: return "ACK";
        case T_NAK: return "NAK";
        case T_FIN: return "FIN";
        case T_FIN_ACK: return "FIN-ACK";
    }
    return "?";
}

std::string buildFrame(const Frame &f) {
    Bytes h = {(std::uint8_t)(FRAME_MAGIC >> 8), (std::uint8_t)(FRAME_MAGIC & 0xFF), 1, f.type, f.scheme, f.flags,
               (std::uint8_t)(f.seq >> 24), (std::uint8_t)(f.seq >> 16), (std::uint8_t)(f.seq >> 8), (std::uint8_t)f.seq,
               (std::uint8_t)(f.dataLen >> 8), (std::uint8_t)f.dataLen, (std::uint8_t)(f.codeBits >> 8),
               (std::uint8_t)f.codeBits};
    std::uint32_t c = headerCrc().compute(h);
    h.push_back((std::uint8_t)(c >> 8));
    h.push_back((std::uint8_t)c);
    std::string d(h.begin(), h.end());
    d.append(f.payload.begin(), f.payload.end());
    return d;
}

bool parseFrame(const std::string &d, Frame &f, bool *crcBad) {
    if (crcBad) *crcBad = false;
    if (d.size() < (size_t)FRAME_HEADER) return false;
    const std::uint8_t *b = (const std::uint8_t *)d.data();
    Bytes h(b, b + 14);
    std::uint16_t want = (std::uint16_t)headerCrc().compute(h);
    std::uint16_t got = (std::uint16_t)((b[14] << 8) | b[15]);
    if (want != got || ((b[0] << 8) | b[1]) != FRAME_MAGIC || b[2] != 1) {
        if (crcBad) *crcBad = true;
        return false;
    }
    f.type = b[3];
    f.scheme = b[4];
    f.flags = b[5];
    f.seq = ((std::uint32_t)b[6] << 24) | ((std::uint32_t)b[7] << 16) | ((std::uint32_t)b[8] << 8) | b[9];
    f.dataLen = (std::uint16_t)((b[10] << 8) | b[11]);
    f.codeBits = (std::uint16_t)((b[12] << 8) | b[13]);
    f.payload.assign(b + FRAME_HEADER, b + d.size());
    return true;
}

static Bits unpack(const Bytes &payload, size_t nbits) {
    Bits b = toBits(payload);
    b.resize(nbits, 0);
    return b;
}

// ------------------------------------------------------------------ receiver
bool Receiver::start(int port, const std::string &out, bool verbose) {
    out_ = out;
    verbose_ = verbose;
    s_ = net::udpSocket("0.0.0.0", port);
    if (s_ == BAD_SOCKET) return false;
    net::setRecvTimeout(s_, 20);
    port_ = net::localPort(s_);
    stop_ = false;
    th_ = std::thread(&Receiver::loop, this);
    return true;
}

void Receiver::stop() {
    if (s_ == BAD_SOCKET) return;
    stop_ = true;
    if (th_.joinable()) th_.join();
    CLOSE_SOCKET(s_);
    s_ = BAD_SOCKET;
}

ReceiverStats Receiver::stats() {
    std::lock_guard<std::mutex> lk(mu_);
    return st_;
}

std::map<std::uint32_t, Bytes> Receiver::frames() {
    std::lock_guard<std::mutex> lk(mu_);
    return frames_;
}

std::string Receiver::data() {
    std::lock_guard<std::mutex> lk(mu_);
    return data_;
}

bool Receiver::waitFinished(int ms) {
    for (int i = 0; i < ms / 10; i++) {
        if (stats().finished) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return stats().finished;
}

void Receiver::reply(std::uint8_t type, std::uint32_t seq, std::uint8_t flags, const net::Addr &to) {
    Frame r;
    r.type = type;
    r.seq = seq;
    r.flags = flags;
    std::string d = buildFrame(r);
    net::sendTo(s_, d.data(), d.size(), to);
}

void Receiver::loop() {
    std::vector<char> buf(70000);
    while (!stop_) {
        net::Addr from;
        int n = net::recvFrom(s_, buf.data(), buf.size(), from);
        if (n <= 0) continue;
        Frame f;
        bool crcBad = false;
        std::lock_guard<std::mutex> lk(mu_);
        if (!parseFrame(std::string(buf.data(), (size_t)n), f, &crcBad)) {
            st_.headerErrors++;   // damaged header: drop silently, the sender times out
            if (verbose_) std::printf("  frame with a damaged header dropped\n");
            continue;
        }
        if (f.type == T_DATA) {
            if (f.seq == 0 && st_.finished) {   // a new transfer begins
                st_ = ReceiverStats();
                expected_ = 0;
                data_.clear();
                frames_.clear();
            }
            auto codec = codecByWire(f.scheme);
            if (!codec) continue;
            st_.scheme = codec->name();
            st_.frames++;
            DecodeResult r = codec->decode(unpack(f.payload, f.codeBits), f.dataLen);
            if (r.status == Status::DETECTED) {
                st_.detected++;
                reply(T_NAK, f.seq, 0, from);
                if (verbose_) std::printf("  frame %u: %s -> NAK\n", f.seq, statusName(r.status));
                continue;
            }
            if (r.status == Status::CORRECTED) {
                st_.corrected++;
                st_.correctedBits += (std::uint64_t)r.correctedBits;
            } else {
                st_.clean++;
            }
            if (f.seq == expected_) {
                data_.append(r.data.begin(), r.data.end());
                frames_[f.seq] = r.data;
                st_.bytes += r.data.size();
                expected_++;
            } else {
                st_.duplicates++;   // our ACK was lost and the frame came again
            }
            if (verbose_)
                std::printf("  frame %u: %s%s -> ACK\n", f.seq, statusName(r.status),
                            r.status == Status::CORRECTED ? (" (" + std::to_string(r.correctedBits) + " bit)").c_str() : "");
            reply(T_ACK, f.seq, 0, from);
        } else if (f.type == T_FIN) {
            CrcCodec c32(CRC32);
            DecodeResult r = c32.decode(unpack(f.payload, f.codeBits), f.dataLen);
            if (r.status != Status::OK || r.data.size() != 40) {
                reply(T_NAK, f.seq, 0, from);
                continue;
            }
            Bytes h(r.data.begin() + 8, r.data.end());
            std::string want;
            char x[3];
            for (auto v : h) {
                std::snprintf(x, sizeof(x), "%02x", v);
                want += x;
            }
            st_.sha = sha256Hex(data_);
            st_.fileMatch = st_.sha == want;
            if (!st_.finished && !out_.empty()) std::ofstream(out_, std::ios::binary) << data_;
            st_.finished = true;
            reply(T_FIN_ACK, f.seq, st_.fileMatch ? 1 : 2, from);
        }
    }
}

// ------------------------------------------------------------------- channel
bool Channel::start(int listenPort, const std::string &host, int serverPort, const ChannelConfig &cfg) {
    cfg_ = cfg;
    rng_.seed(cfg.seed);
    for (auto &g : gilbert_) {
        g.pGoodToBad = cfg.pGoodToBad;
        g.pBadToGood = cfg.pBadToGood;
        g.berBad = cfg.berBad;
        g.berGood = 0;
    }
    if (!net::resolve(host, serverPort, server_)) return false;
    main_ = net::udpSocket("0.0.0.0", listenPort);
    up_ = net::udpSocket("0.0.0.0", 0);
    if (main_ == BAD_SOCKET || up_ == BAD_SOCKET) return false;
    port_ = net::localPort(main_);
    if (!cfg.pcap.empty()) pcap_.open(cfg.pcap);
    stop_ = false;
    th_ = std::thread(&Channel::loop, this);
    return true;
}

void Channel::stop() {
    if (main_ == BAD_SOCKET) return;
    stop_ = true;
    if (th_.joinable()) th_.join();
    CLOSE_SOCKET(main_);
    CLOSE_SOCKET(up_);
    main_ = up_ = BAD_SOCKET;
    pcap_.close();
}

ChannelStats Channel::stats() {
    std::lock_guard<std::mutex> lk(mu_);
    return st_;
}

void Channel::setConfig(const ChannelConfig &c) {
    std::lock_guard<std::mutex> lk(mu_);
    cfg_ = c;
}

std::string Channel::damage(const std::string &d, int dir) {
    st_.datagrams++;
    Bytes bytes(d.begin(), d.end());
    Bits bits = toBits(bytes);
    size_t from = cfg_.protectHeader ? FRAME_HEADER * 8 : 0;
    Bits part(bits.begin() + (long)std::min(from, bits.size()), bits.end());
    st_.bits += part.size();
    std::vector<size_t> flipped = cfg_.burst ? gilbert_[dir].apply(part, rng_) : flipBer(part, cfg_.ber, rng_);
    if (flipped.empty()) return d;
    st_.corrupted++;
    st_.bitsFlipped += flipped.size();
    std::copy(part.begin(), part.end(), bits.begin() + (long)from);
    Bytes out = toBytes(bits);
    if (cfg_.verbose) std::printf("  channel: %zu bit(s) flipped in a %s datagram\n", flipped.size(), dir ? "reply" : "data");
    return std::string(out.begin(), out.end());
}

void Channel::loop() {
    std::vector<char> buf(70000);
    std::uniform_real_distribution<double> u(0, 1);
    while (!stop_) {
        double wait = 20;
        {
            std::lock_guard<std::mutex> lk(mu_);
            double now = net::nowMs();
            while (!q_.empty() && q_.top().t <= now) {
                const Item &it = q_.top();
                net::sendTo(it.via, it.data.data(), it.data.size(), it.to);
                if (it.dir == 0)
                    pcap_.write(0x0A000001, 40000, 0x0A000064, server_.port(), it.data);
                else
                    pcap_.write(0x0A000064, server_.port(), 0x0A000001, 40000, it.data);
                q_.pop();
            }
            if (!q_.empty()) wait = std::min(wait, q_.top().t - now);
        }
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(main_, &rs);
        FD_SET(up_, &rs);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = (long)(std::max(0.0, wait) * 1000);
        int r = select((int)std::max(main_, up_) + 1, &rs, nullptr, nullptr, &tv);
        if (r <= 0) continue;
        for (int dir = 0; dir < 2; dir++) {
            sock_t s = dir == 0 ? main_ : up_;
            if (!FD_ISSET(s, &rs)) continue;
            net::Addr from;
            int n = net::recvFrom(s, buf.data(), buf.size(), from);
            if (n <= 0) continue;
            std::lock_guard<std::mutex> lk(mu_);
            if (dir == 0) {
                client_ = from;
                haveClient_ = true;
            } else if (!haveClient_) {
                continue;
            }
            if (cfg_.drop > 0 && u(rng_) < cfg_.drop) {
                st_.dropped++;
                st_.datagrams++;
                continue;
            }
            std::string out = damage(std::string(buf.data(), (size_t)n), dir);
            q_.push({net::nowMs() + cfg_.delayMs, n_++, dir == 0 ? up_ : main_, dir == 0 ? server_ : client_, out, dir});
        }
    }
}

// -------------------------------------------------------------------- sender
SendResult sendData(const std::string &host, int port, const Codec &codec, const std::string &data,
                    const SendOptions &opt) {
    SendResult res;
    net::Addr to;
    if (!net::resolve(host, port, to)) {
        res.error = "cannot resolve " + host;
        return res;
    }
    sock_t s = net::udpSocket("0.0.0.0", 0);
    net::setRecvTimeout(s, 5);
    std::vector<char> buf(70000);
    size_t nFrames = (data.size() + opt.frameBytes - 1) / opt.frameBytes;
    res.frames = nFrames;
    double t0 = net::nowMs();

    // send one frame until it is acknowledged; returns the reply frame
    auto exchange = [&](const Frame &f, std::uint8_t wantType, Frame &reply) -> bool {
        std::string d = buildFrame(f);
        for (int attempt = 0; attempt <= opt.maxRetries; attempt++) {
            net::sendTo(s, d.data(), d.size(), to);
            res.sent++;
            double deadline = net::nowMs() + opt.timeoutMs;
            bool nak = false;
            while (net::nowMs() < deadline) {
                net::Addr from;
                int n = net::recvFrom(s, buf.data(), buf.size(), from);
                if (n <= 0) continue;
                Frame r;
                if (!parseFrame(std::string(buf.data(), (size_t)n), r)) {
                    res.badReplies++;   // damaged ACK/NAK: ignore, wait for the timeout
                    continue;
                }
                if (r.seq != f.seq) continue;   // old reply
                if (r.type == wantType) {
                    reply = r;
                    return true;
                }
                if (r.type == T_NAK) {
                    nak = true;
                    break;
                }
            }
            if (attempt == opt.maxRetries) break;
            if (nak)
                res.nakRetx++;
            else
                res.timeoutRetx++;
        }
        return false;
    };

    for (size_t i = 0; i < nFrames; i++) {
        size_t off = i * opt.frameBytes, len = std::min((size_t)opt.frameBytes, data.size() - off);
        Bytes block(data.begin() + (long)off, data.begin() + (long)(off + len));
        Bits cw = codec.encode(block);
        Frame f;
        f.type = T_DATA;
        f.scheme = codec.wireId();
        f.seq = (std::uint32_t)i;
        f.dataLen = (std::uint16_t)len;
        f.codeBits = (std::uint16_t)cw.size();
        f.payload = toBytes(cw);
        double ft = net::nowMs();
        Frame r;
        if (!exchange(f, T_ACK, r)) {
            res.error = "frame " + std::to_string(i) + " not acknowledged after " + std::to_string(opt.maxRetries) +
                        " retries";
            CLOSE_SOCKET(s);
            return res;
        }
        double ms = net::nowMs() - ft;
        res.frameMs.push_back(ms);
        res.maxFrameMs = std::max(res.maxFrameMs, ms);
        if (opt.progress && (i % 64 == 0 || i + 1 == nFrames)) {
            std::printf("\r  frame %zu/%zu sent, retransmissions %llu", i + 1, nFrames,
                        (unsigned long long)(res.nakRetx + res.timeoutRetx));
            std::fflush(stdout);
        }
    }
    if (opt.progress) std::printf("\n");

    // FIN carries the file size and SHA-256, always protected by CRC-32
    std::string sha = sha256Hex(data);
    Bytes fin(8, 0);
    for (int k = 0; k < 8; k++) fin[k] = (std::uint8_t)((std::uint64_t)data.size() >> (56 - 8 * k));
    for (size_t k = 0; k < 64; k += 2) fin.push_back((std::uint8_t)std::stoi(sha.substr(k, 2), nullptr, 16));
    CrcCodec c32(CRC32);
    Bits cw = c32.encode(fin);
    Frame f;
    f.type = T_FIN;
    f.scheme = c32.wireId();
    f.seq = (std::uint32_t)nFrames;
    f.dataLen = (std::uint16_t)fin.size();
    f.codeBits = (std::uint16_t)cw.size();
    f.payload = toBytes(cw);
    Frame r;
    bool finOk = exchange(f, T_FIN_ACK, r);
    res.seconds = (net::nowMs() - t0) / 1000.0;
    CLOSE_SOCKET(s);
    if (!finOk) {
        res.error = "no FIN-ACK from the receiver";
        return res;
    }
    res.ok = true;
    res.fileMatch = r.flags == 1;
    double sum = 0;
    for (double x : res.frameMs) sum += x;
    res.avgFrameMs = res.frameMs.empty() ? 0 : sum / res.frameMs.size();
    res.goodputKbps = res.seconds > 0 ? data.size() * 8.0 / res.seconds / 1000.0 : 0;
    res.efficiency = res.sent ? (double)(nFrames + 1) / res.sent : 0;
    return res;
}

}  // namespace edc
