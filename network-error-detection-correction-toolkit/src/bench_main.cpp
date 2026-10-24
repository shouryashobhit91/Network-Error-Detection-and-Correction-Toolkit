// edc_bench - performance and accuracy experiments. Writes results/B*.csv and
// graphs/B*.svg.
//
//   edc_bench                 all experiments (a few minutes)
//   edc_bench --only B1,B5    selected experiments
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>

#include "args.h"
#include "codes.h"
#include "errors.h"
#include "link.h"
#include "svgplot.h"

using namespace edc;

static std::mt19937 rng(2026);
static const char *COL[] = {"#7b8794", "#f4a261", "#2a9d8f", "#9b5de5", "#1f6feb", "#d1495b", "#e9c46a", "#264653", "#06d6a0"};

static Bytes randomBytes(size_t n) {
    Bytes b(n);
    for (auto &x : b) x = (std::uint8_t)rng();
    return b;
}

struct Tally {
    long long clean = 0, ok = 0, corrected = 0, detected = 0, undetected = 0;
    long long hit() const { return std::max(1LL, ok + corrected + detected + undetected); }
};

// one trial: encode random data, damage the codeword, decode, classify
template <class F>
static Tally trials(const Codec &c, size_t n, int count, F damage) {
    Tally t;
    for (int i = 0; i < count; i++) {
        Bytes d = randomBytes(n);
        Bits cw = c.encode(d);
        auto f = damage(cw);
        DecodeResult r = c.decode(cw, n);
        if (f.empty()) {
            t.clean++;
            continue;
        }
        if (r.status == Status::DETECTED) t.detected++;
        else if (r.data == d) (r.status == Status::CORRECTED ? t.corrected : t.ok)++;
        else t.undetected++;
    }
    return t;
}

// ------------------------------------------------------------------- B1
static void b1() {
    std::printf("\n[B1] random bit errors in a 64-byte frame (20000 frames per point)\n");
    std::ofstream f("results/B1_random_errors.csv");
    f << "code,errors,detected_pct,corrected_pct,undetected_pct\n";
    auto codes = allCodecs();
    plot::Chart cu, cc;
    cu.title = "Wrong data accepted vs number of bit errors (64-byte frame)";
    cu.width = 980;
    cu.xlabel = "number of bit errors in the frame";
    cu.ylabel = "undetected / wrong frames (%)";
    cu.legendPos = "outside";
    cc.title = "Frames delivered correctly vs number of bit errors";
    cc.xlabel = "number of bit errors in the frame";
    cc.ylabel = "detected or corrected (%)";
    std::printf("  %-31s", "code \\ errors");
    for (int k = 1; k <= 8; k++) std::printf("  k=%d   ", k);
    std::printf("   (undetected %%)\n");
    for (size_t i = 0; i < codes.size(); i++) {
        plot::Series su(codes[i]->name(), COL[i]), sc(codes[i]->name(), COL[i]);
        std::printf("  %-31s", codes[i]->name().c_str());
        for (int k = 1; k <= 8; k++) {
            Tally t = trials(*codes[i], 64, 20000, [&](Bits &b) { return flipRandom(b, k, rng); });
            double det = 100.0 * t.detected / t.hit(), cor = 100.0 * t.corrected / t.hit(),
                   und = 100.0 * t.undetected / t.hit();
            f << codes[i]->id() << "," << k << "," << det << "," << cor << "," << und << "\n";
            su.pts.push_back({(double)k, und});
            sc.pts.push_back({(double)k, det + cor});
            std::printf(" %6.2f ", und);
        }
        std::printf("\n");
        std::fflush(stdout);
        cu.series.push_back(su);
        cc.series.push_back(sc);
    }
    plot::save("graphs/B1_undetected_vs_errors.svg", plot::lineChart(cu));

}

// ------------------------------------------------------------------- B2
static void b2() {
    std::printf("\n[B2] burst errors in a 64-byte frame (5000 frames per point)\n");
    std::ofstream f("results/B2_burst_errors.csv");
    f << "code,burst_length,detected_pct,corrected_pct,undetected_pct\n";
    plot::Chart c;
    c.title = "Burst errors: frames detected or corrected (64-byte frame)";
    c.width = 980;
    c.xlabel = "burst length (bits)";
    c.ylabel = "detected or corrected (%)";
    c.ymin = 0;
    c.ymax = 105;
    c.legendPos = "outside";
    std::vector<int> lens = {1, 2, 4, 8, 9, 12, 16, 17, 24, 32, 33, 40, 48, 64};
    std::vector<std::string> ids = {"parity", "parity2d", "checksum", "crc8", "crc16", "crc32", "hamming", "hamming-il"};
    std::printf("  %-31s", "code \\ burst");
    for (int L : lens) std::printf(" %4d", L);
    std::printf("   (detected+corrected %%)\n");
    for (size_t i = 0; i < ids.size(); i++) {
        auto cd = codecById(ids[i]);
        plot::Series s(cd->name(), COL[i]);
        std::printf("  %-31s", cd->name().c_str());
        for (int L : lens) {
            Tally t = trials(*cd, 64, 5000, [&](Bits &b) { return flipBurstRandom(b, L, rng); });
            double det = 100.0 * t.detected / t.hit(), cor = 100.0 * t.corrected / t.hit(),
                   und = 100.0 * t.undetected / t.hit();
            f << ids[i] << "," << L << "," << det << "," << cor << "," << und << "\n";
            s.pts.push_back({(double)L, det + cor});
            std::printf(" %4.0f", det + cor);
        }
        std::printf("\n");
        std::fflush(stdout);
        c.series.push_back(s);
    }
    plot::save("graphs/B2_burst_detection.svg", plot::lineChart(c));
}

// ------------------------------------------------------------------- B3
static void b3() {
    std::printf("\n[B3] random channel bit error rate, 64-byte frames (20000 frames per point)\n");
    std::ofstream f("results/B3_ber.csv");
    f << "code,ber,clean_pct,corrected_pct,detected_pct,undetected_pct\n";
    std::vector<double> bers = {1e-4, 3e-4, 1e-3, 3e-3, 1e-2, 3e-2};
    plot::Chart cu, cg;
    cu.title = "Frames with wrong data accepted vs bit error rate";
    cu.xlabel = "bit error rate (log scale)";
    cu.ylabel = "undetected frames (% of all frames, log)";
    cu.logx = true;
    cu.logy = true;
    cu.ymin = 0.001;
    cu.width = 980;
    cu.legendPos = "outside";
    cg.title = "Frames accepted without retransmission vs bit error rate";
    cg.xlabel = "bit error rate (log scale)";
    cg.ylabel = "correct on first try (%)";
    cg.logx = true;
    cg.ymin = 0;
    cg.ymax = 105;
    cg.width = 980;
    cg.legendPos = "outside";
    auto codes = allCodecs();
    std::printf("  %-31s", "code \\ BER");
    for (double b : bers) std::printf(" %8.0e", b);
    std::printf("   (undetected %% / good first try %%)\n");
    for (size_t i = 0; i < codes.size(); i++) {
        plot::Series su(codes[i]->name(), COL[i]), sg(codes[i]->name(), COL[i]);
        std::printf("  %-31s", codes[i]->name().c_str());
        for (double ber : bers) {
            Tally t = trials(*codes[i], 64, 20000, [&](Bits &b) { return flipBer(b, ber, rng); });
            double all = 20000.0;
            double und = 100 * t.undetected / all, good = 100 * (t.clean + t.ok + t.corrected) / all;
            f << codes[i]->id() << "," << ber << "," << 100 * t.clean / all << "," << 100 * t.corrected / all << ","
              << 100 * t.detected / all << "," << und << "\n";
            if (und > 0) su.pts.push_back({ber, und});
            sg.pts.push_back({ber, good});
            std::printf(" %4.2f/%3.0f", und, good);
        }
        std::printf("\n");
        std::fflush(stdout);
        if (!su.pts.empty()) cu.series.push_back(su);
        cg.series.push_back(sg);
    }
    cu.xmin = 1e-4;
    cu.xmax = 3e-2;
    cg.xmin = 1e-4;
    cg.xmax = 3e-2;
    plot::save("graphs/B3_undetected_vs_ber.svg", plot::lineChart(cu));
    plot::save("graphs/B3_first_try_vs_ber.svg", plot::lineChart(cg));
}

// ------------------------------------------------------------------- B4
static void b4() {
    std::printf("\n[B4] overhead and processing speed (1 MB in 64-byte frames)\n");
    std::ofstream f("results/B4_overhead_speed.csv");
    f << "code,codeword_bits_64B,overhead_pct,encode_MBps,decode_MBps,detects,corrects\n";
    std::vector<plot::BarGroup> gOver, gSpeed;
    Bytes block = randomBytes(64);
    std::printf("  %-31s %8s %9s %10s %10s\n", "code", "bits", "overhead", "enc MB/s", "dec MB/s");
    for (auto &c : allCodecs()) {
        const int N = 16384;   // 1 MB
        Bits cw;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; i++) {
            block[0] = (std::uint8_t)i;
            cw = c->encode(block);
        }
        auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; i++) c->decode(cw, 64);
        auto t2 = std::chrono::steady_clock::now();
        double enc = N * 64.0 / 1e6 / std::chrono::duration<double>(t1 - t0).count();
        double dec = N * 64.0 / 1e6 / std::chrono::duration<double>(t2 - t1).count();
        f << c->id() << "," << cw.size() << "," << c->overheadPercent(64) << "," << enc << "," << dec << ","
          << "yes," << (c->corrects() ? "yes" : "no") << "\n";
        std::printf("  %-31s %8zu %8.1f%% %10.1f %10.1f\n", c->name().c_str(), cw.size(), c->overheadPercent(64), enc, dec);
        std::string shortName = c->id();
        gOver.push_back({shortName, {c->overheadPercent(64)}});
        gSpeed.push_back({shortName, {enc, dec}});
    }
    plot::save("graphs/B4_overhead.svg",
               plot::barChart("Redundancy overhead for a 64-byte frame", "extra bits (% of data bits)",
                              {"overhead"}, {"#1f6feb"}, gOver, 900, 440));
    plot::save("graphs/B4_speed.svg",
               plot::barChart("Encoding and decoding speed (this implementation)", "MB per second",
                              {"encode", "decode"}, {"#2a9d8f", "#f4a261"}, gSpeed, 900, 440));
}

// ------------------------------------------------------------------- B5
struct NetPoint {
    SendResult s;
    ReceiverStats r;
    ChannelStats c;
};

static NetPoint netRun(const std::string &id, double ber, int frame, size_t bytes, unsigned seed, bool burst = false) {
    NetPoint p;
    Receiver rx;
    rx.start(0);
    ChannelConfig cc;
    cc.ber = ber;
    cc.burst = burst;
    cc.delayMs = 1;
    cc.seed = seed;
    Channel ch;
    ch.start(0, "127.0.0.1", rx.port(), cc);
    std::mt19937 g(seed);
    std::string data(bytes, '\0');
    for (auto &x : data) x = (char)g();
    SendOptions o;
    o.frameBytes = frame;
    o.timeoutMs = 30;
    o.maxRetries = 200;
    p.s = sendData("127.0.0.1", ch.port(), *codecById(id), data, o);
    rx.waitFinished(300);
    p.r = rx.stats();
    p.c = ch.stats();
    return p;
}

static void b5() {
    std::printf("\n[B5] file transfer over the noisy UDP channel (32 KB, 64-byte frames, stop-and-wait ARQ, 1 ms delay)\n");
    std::ofstream f("results/B5_network.csv");
    f << "code,ber,ok,file_identical,seconds,goodput_kbps,transmissions,retx_nak,retx_timeout,corrected_frames,"
         "detected_frames,avg_frame_ms\n";
    std::vector<double> bers = {0, 1e-4, 3e-4, 1e-3, 2e-3, 3e-3};
    std::vector<std::string> ids = {"parity", "checksum", "crc32", "hamming", "secded"};
    std::vector<std::string> cols = {COL[0], COL[2], COL[5], COL[6], COL[7]};
    plot::Chart cg, cr;
    cg.title = "Goodput of the file transfer vs bit error rate";
    cg.xlabel = "channel bit error rate";
    cg.ylabel = "goodput (kbit/s)";
    cg.xmin = 0;
    cr.title = "Retransmissions vs bit error rate (32 KB = 512 frames)";
    cr.xlabel = "channel bit error rate";
    cr.ylabel = "retransmitted frames";
    cr.xmin = 0;
    cr.legendPos = "left";
    std::printf("  %-26s %7s %8s %9s %6s %6s %6s %9s %s\n", "code", "BER", "time s", "kbit/s", "sends", "NAK", "tmout",
                "corrected", "file");
    for (size_t i = 0; i < ids.size(); i++) {
        auto cd = codecById(ids[i]);
        plot::Series sg(cd->name(), cols[i]), sr(cd->name(), cols[i]);
        for (double ber : bers) {
            NetPoint p = netRun(ids[i], ber, 64, 32768, 11);
            f << ids[i] << "," << ber << "," << p.s.ok << "," << p.s.fileMatch << "," << p.s.seconds << ","
              << p.s.goodputKbps << "," << p.s.sent << "," << p.s.nakRetx << "," << p.s.timeoutRetx << ","
              << p.r.corrected << "," << p.r.detected << "," << p.s.avgFrameMs << "\n";
            sg.pts.push_back({ber, p.s.goodputKbps});
            sr.pts.push_back({ber, (double)(p.s.nakRetx + p.s.timeoutRetx)});
            std::printf("  %-26s %7.0e %8.2f %9.1f %6llu %6llu %6llu %9llu %s\n", cd->name().c_str(), ber, p.s.seconds,
                        p.s.goodputKbps, (unsigned long long)p.s.sent, (unsigned long long)p.s.nakRetx,
                        (unsigned long long)p.s.timeoutRetx, (unsigned long long)p.r.corrected,
                        !p.s.ok ? "FAILED" : (p.s.fileMatch ? "identical" : "CORRUPTED"));
            std::fflush(stdout);
        }
        cg.series.push_back(sg);
        cr.series.push_back(sr);
    }
    plot::save("graphs/B5_goodput_vs_ber.svg", plot::lineChart(cg));
    plot::save("graphs/B5_retransmissions_vs_ber.svg", plot::lineChart(cr));
}

// ------------------------------------------------------------------- B6
static void b6() {
    std::printf("\n[B6] frame size at BER 1e-3 (32 KB file)\n");
    std::ofstream f("results/B6_frame_size.csv");
    f << "code,frame_bytes,ok,seconds,goodput_kbps,transmissions,retransmissions,file_identical\n";
    plot::Chart c;
    c.title = "Goodput vs frame size at bit error rate 1e-3";
    c.xlabel = "data bytes per frame (log scale)";
    c.ylabel = "goodput (kbit/s)";
    c.logx = true;
    c.legendPos = "left";
    std::vector<std::string> ids = {"crc32", "hamming"};
    std::vector<std::string> cols = {COL[5], COL[6]};
    std::printf("  %-20s %6s %8s %9s %8s\n", "code", "frame", "time s", "kbit/s", "retx");
    for (size_t i = 0; i < ids.size(); i++) {
        auto cd = codecById(ids[i]);
        plot::Series s(cd->name(), cols[i]);
        for (int fr : {16, 32, 64, 128, 256, 512}) {
            NetPoint p = netRun(ids[i], 1e-3, fr, 32768, 21);
            f << ids[i] << "," << fr << "," << p.s.ok << "," << p.s.seconds << "," << p.s.goodputKbps << "," << p.s.sent << ","
              << p.s.nakRetx + p.s.timeoutRetx << "," << p.s.fileMatch << "\n";
            s.pts.push_back({(double)fr, p.s.ok ? p.s.goodputKbps : 0.0});
            std::printf("  %-20s %6d %8.2f %9.1f %8llu %s\n", cd->name().c_str(), fr, p.s.seconds, p.s.goodputKbps,
                        (unsigned long long)(p.s.nakRetx + p.s.timeoutRetx),
                        !p.s.ok ? ("FAILED: " + p.s.error).c_str() : (p.s.fileMatch ? "file identical" : "file CORRUPTED"));
            std::fflush(stdout);
        }
        c.series.push_back(s);
    }
    plot::save("graphs/B6_frame_size.svg", plot::lineChart(c));
}

int main(int argc, char **argv) {
    Args a(argc, argv);
    net::start();
    std::filesystem::create_directories("results");
    std::filesystem::create_directories("graphs");
    std::set<std::string> only;
    std::string o = a.get("only");
    for (size_t p = 0; p < o.size();) {
        size_t k = o.find(',', p);
        only.insert(o.substr(p, k == std::string::npos ? std::string::npos : k - p));
        if (k == std::string::npos) break;
        p = k + 1;
    }
    auto want = [&](const char *e) { return only.empty() || only.count(e); };
    auto t0 = std::chrono::steady_clock::now();
    if (want("B1")) b1();
    if (want("B2")) b2();
    if (want("B3")) b3();
    if (want("B4")) b4();
    if (want("B5")) b5();
    if (want("B6")) b6();
    std::printf("\nfinished in %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    net::stop();
    return 0;
}
