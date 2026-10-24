// edc_receiver - receives frames, detects / corrects errors, sends ACK or NAK,
// and checks the received file with SHA-256.
//
//   edc_receiver [--port 9100] [--out received.bin] [--verbose]
#include <csignal>
#include <cstdio>
#include <thread>

#include "args.h"
#include "link.h"

static volatile std::sig_atomic_t gStop = 0;
static void onSig(int) { gStop = 1; }

int main(int argc, char **argv) {
    Args a(argc, argv);
    net::start();
    edc::Receiver rx;
    int port = (int)a.num("port", 9100);
    if (!rx.start(port, a.get("out", "received.bin"), a.has("verbose"))) {
        std::printf("cannot bind UDP port %d\n", port);
        return 1;
    }
    std::signal(SIGINT, onSig);
    std::printf("Receiver listening on UDP port %d, file will be saved as %s (Ctrl+C to stop)\n", rx.port(),
                a.get("out", "received.bin").c_str());
    bool reported = false;
    while (!gStop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        edc::ReceiverStats s = rx.stats();
        if (s.finished && !reported) {
            reported = true;
            std::printf("\nTransfer finished, code: %s\n", s.scheme.c_str());
            std::printf("  frames received        : %llu\n", (unsigned long long)s.frames);
            std::printf("  clean frames           : %llu\n", (unsigned long long)s.clean);
            std::printf("  corrected frames       : %llu (%llu bits corrected)\n", (unsigned long long)s.corrected,
                        (unsigned long long)s.correctedBits);
            std::printf("  errors detected (NAK)  : %llu\n", (unsigned long long)s.detected);
            std::printf("  damaged headers dropped: %llu\n", (unsigned long long)s.headerErrors);
            std::printf("  duplicate frames       : %llu\n", (unsigned long long)s.duplicates);
            std::printf("  bytes delivered        : %llu\n", (unsigned long long)s.bytes);
            std::printf("  SHA-256 of received    : %s\n", s.sha.c_str());
            std::printf("  file integrity         : %s\n",
                        s.fileMatch ? "OK, identical to the sent file"
                                    : "CORRUPTED, undetected errors reached the application");
            std::fflush(stdout);
        } else if (!s.finished) {
            reported = false;
        }
    }
    rx.stop();
    net::stop();
    return 0;
}
