// edc_channel - noisy channel between sender and receiver. Flips bits with a
// random bit error rate or in bursts, can drop and delay datagrams, and can save
// what it delivers to a pcap file for Wireshark.
//
//   edc_channel --listen 9101 --server 127.0.0.1:9100 [--ber 1e-4]
//               [--burst 0.0005,0.1] [--drop 0.01] [--delay 2] [--pcap noisy.pcap] [--seed 1]
#include <csignal>
#include <cstdio>
#include <thread>

#include "args.h"
#include "link.h"

static volatile std::sig_atomic_t gStop = 0;
static void onSig(int) { gStop = 1; }

int main(int argc, char **argv) {
    Args a(argc, argv);
    if (!a.has("server")) {
        std::printf("usage: edc_channel --listen 9101 --server 127.0.0.1:9100 [--ber p] [--burst g2b,b2g]\n"
                    "                   [--drop p] [--delay ms] [--pcap file] [--seed n]\n");
        return 1;
    }
    net::start();
    edc::ChannelConfig c;
    c.ber = a.num("ber", 0);
    if (a.has("burst")) {
        std::string b = a.get("burst");
        size_t k = b.find(',');
        c.burst = true;
        c.pGoodToBad = std::atof(b.substr(0, k).c_str());
        if (k != std::string::npos) c.pBadToGood = std::atof(b.substr(k + 1).c_str());
    }
    c.drop = a.num("drop", 0);
    c.delayMs = a.num("delay", 0);
    c.seed = (unsigned)a.num("seed", 1);
    c.pcap = a.get("pcap");
    c.verbose = a.has("verbose");
    std::string srv = a.get("server");
    size_t colon = srv.find(':');
    edc::Channel ch;
    if (!ch.start((int)a.num("listen", 9101), srv.substr(0, colon),
                  colon == std::string::npos ? 9100 : std::atoi(srv.substr(colon + 1).c_str()), c)) {
        std::printf("cannot start the channel\n");
        return 1;
    }
    std::signal(SIGINT, onSig);
    if (c.burst)
        std::printf("Channel UDP %d -> %s, burst errors (good->bad %.4f, bad->good %.2f, BER in burst 0.5)", ch.port(),
                    srv.c_str(), c.pGoodToBad, c.pBadToGood);
    else
        std::printf("Channel UDP %d -> %s, random bit error rate %g", ch.port(), srv.c_str(), c.ber);
    std::printf(", drop %g, delay %g ms%s\n", c.drop, c.delayMs, c.pcap.empty() ? "" : (", capture " + c.pcap).c_str());
    std::fflush(stdout);
    edc::ChannelStats last;
    while (!gStop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        edc::ChannelStats s = ch.stats();
        if (s.datagrams != last.datagrams && s.datagrams % 500 < 5) {
            std::printf("  datagrams %llu, corrupted %llu, bits flipped %llu, dropped %llu\n",
                        (unsigned long long)s.datagrams, (unsigned long long)s.corrupted,
                        (unsigned long long)s.bitsFlipped, (unsigned long long)s.dropped);
            std::fflush(stdout);
        }
        last = s;
    }
    edc::ChannelStats s = ch.stats();
    std::printf("\nfinal: datagrams %llu, corrupted %llu, bits flipped %llu of %llu (measured BER %.2e), dropped %llu\n",
                (unsigned long long)s.datagrams, (unsigned long long)s.corrupted, (unsigned long long)s.bitsFlipped,
                (unsigned long long)s.bits, s.bits ? (double)s.bitsFlipped / s.bits : 0.0, (unsigned long long)s.dropped);
    ch.stop();
    net::stop();
    return 0;
}
