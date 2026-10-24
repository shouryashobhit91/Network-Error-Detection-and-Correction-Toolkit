// edc_sender - sends a file in frames protected by the chosen code, using
// stop-and-wait ARQ (retransmit on NAK or timeout).
//
//   edc_sender <file> [--host 127.0.0.1] [--port 9101] [--scheme crc32]
//              [--frame 64] [--timeout 60] [--retries 50]
//   edc_sender --random 65536 ...     send random data instead of a file
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

#include "args.h"
#include "link.h"
#include "sha256.h"

int main(int argc, char **argv) {
    Args a(argc, argv);
    std::string data, name;
    if (a.has("random")) {
        std::mt19937 g(42);
        data.resize((size_t)a.num("random", 65536));
        for (auto &c : data) c = (char)g();
        name = "random data";
    } else if (!a.pos.empty()) {
        std::ifstream f(a.pos[0], std::ios::binary);
        if (!f) {
            std::printf("cannot open %s\n", a.pos[0].c_str());
            return 1;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        data = ss.str();
        name = a.pos[0];
        size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
    } else {
        std::printf("usage: edc_sender <file> [--host h] [--port 9101] [--scheme crc32] [--frame 64]\n");
        return 1;
    }
    auto codec = edc::codecById(a.get("scheme", "crc32"));
    if (!codec) {
        std::printf("unknown scheme; use one of:");
        for (auto &c : edc::allCodecs()) std::printf(" %s", c->id().c_str());
        std::printf("\n");
        return 1;
    }
    net::start();
    edc::SendOptions o;
    o.frameBytes = (int)a.num("frame", 64);
    o.timeoutMs = (int)a.num("timeout", 60);
    o.maxRetries = (int)a.num("retries", 50);
    o.progress = true;
    std::printf("Sending %s (%zu bytes) with %s, %d data bytes per frame (%zu-bit codeword)\n", name.c_str(),
                data.size(), codec->name().c_str(), o.frameBytes, codec->codewordBits((size_t)o.frameBytes));
    edc::SendResult r = edc::sendData(a.get("host", "127.0.0.1"), (int)a.num("port", 9101), *codec, data, o);
    if (!r.ok) std::printf("ERROR: %s\n", r.error.c_str());
    std::printf("  frames                 : %llu (+1 FIN)\n", (unsigned long long)r.frames);
    std::printf("  transmissions          : %llu  (efficiency %.1f %%)\n", (unsigned long long)r.sent, 100 * r.efficiency);
    std::printf("  retransmissions        : %llu after NAK, %llu after timeout\n", (unsigned long long)r.nakRetx,
                (unsigned long long)r.timeoutRetx);
    std::printf("  damaged replies ignored: %llu\n", (unsigned long long)r.badReplies);
    std::printf("  time / goodput         : %.2f s / %.1f kbit/s\n", r.seconds, r.goodputKbps);
    std::printf("  frame delay avg / max  : %.2f ms / %.2f ms\n", r.avgFrameMs, r.maxFrameMs);
    std::printf("  SHA-256 sent           : %s\n", edc::sha256Hex(data).c_str());
    std::printf("  receiver says          : %s\n", !r.ok ? "-" : (r.fileMatch ? "file identical" : "FILE CORRUPTED"));
    net::stop();
    return r.ok && r.fileMatch ? 0 : 2;
}
