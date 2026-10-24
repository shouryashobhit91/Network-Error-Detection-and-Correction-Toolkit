// errors.cpp - error injection.
#include "errors.h"

#include <algorithm>
#include <sstream>

namespace edc {

std::vector<size_t> flipBits(Bits &bits, const std::vector<size_t> &pos) {
    std::vector<size_t> done;
    for (size_t p : pos)
        if (p < bits.size()) {
            bits[p] ^= 1;
            done.push_back(p);
        }
    return done;
}

std::vector<size_t> flipRandom(Bits &bits, int k, std::mt19937 &rng) {
    std::vector<size_t> idx(bits.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    k = std::min<int>(k, (int)bits.size());
    for (int i = 0; i < k; i++) {   // partial Fisher-Yates shuffle: k distinct positions
        std::uniform_int_distribution<size_t> d(i, idx.size() - 1);
        std::swap(idx[i], idx[d(rng)]);
    }
    std::vector<size_t> pos(idx.begin(), idx.begin() + k);
    std::sort(pos.begin(), pos.end());
    return flipBits(bits, pos);
}

std::vector<size_t> flipBurst(Bits &bits, size_t start, int length, std::mt19937 &rng) {
    std::vector<size_t> pos;
    if (length <= 0 || start >= bits.size()) return pos;
    size_t end = std::min(bits.size() - 1, start + (size_t)length - 1);
    std::bernoulli_distribution half(0.5);
    for (size_t i = start; i <= end; i++)
        if (i == start || i == end || half(rng)) pos.push_back(i);
    return flipBits(bits, pos);
}

std::vector<size_t> flipBurstRandom(Bits &bits, int length, std::mt19937 &rng) {
    if ((size_t)length > bits.size()) length = (int)bits.size();
    std::uniform_int_distribution<size_t> d(0, bits.size() - (size_t)length);
    return flipBurst(bits, d(rng), length, rng);
}

std::vector<size_t> flipBer(Bits &bits, double ber, std::mt19937 &rng) {
    std::vector<size_t> pos;
    if (ber <= 0) return pos;
    // geometric gaps between errors: fast even for very small ber
    std::geometric_distribution<long long> gap(ber);
    long long i = gap(rng);
    while (i < (long long)bits.size()) {
        pos.push_back((size_t)i);
        i += 1 + gap(rng);
    }
    return flipBits(bits, pos);
}

std::vector<size_t> BurstChannel::apply(Bits &bits, std::mt19937 &rng) {
    std::uniform_real_distribution<double> u(0, 1);
    std::vector<size_t> pos;
    for (size_t i = 0; i < bits.size(); i++) {
        if (!bad && u(rng) < pGoodToBad) bad = true;
        else if (bad && u(rng) < pBadToGood) bad = false;
        if (u(rng) < (bad ? berBad : berGood)) pos.push_back(i);
    }
    return flipBits(bits, pos);
}

std::vector<size_t> parsePositions(const std::string &s) {
    std::vector<size_t> v;
    std::string t = s;
    std::replace(t.begin(), t.end(), ',', ' ');
    std::istringstream in(t);
    long long x;
    while (in >> x)
        if (x >= 0) v.push_back((size_t)x);
    return v;
}

}  // namespace edc
