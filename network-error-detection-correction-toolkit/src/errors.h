// errors.h - ways to damage a codeword (error injection) and a simple
// two-state burst channel model.
#pragma once

#include <random>
#include <string>
#include <vector>

#include "codes.h"

namespace edc {

// flip the given bit positions (0 = first bit); returns the positions really flipped
std::vector<size_t> flipBits(Bits &bits, const std::vector<size_t> &positions);

// flip k different random bits
std::vector<size_t> flipRandom(Bits &bits, int k, std::mt19937 &rng);

// burst of length L starting at 'start': first and last bit are flipped, the bits
// in between are flipped with probability 1/2 (standard definition of a burst)
std::vector<size_t> flipBurst(Bits &bits, size_t start, int length, std::mt19937 &rng);
std::vector<size_t> flipBurstRandom(Bits &bits, int length, std::mt19937 &rng);

// every bit is flipped independently with probability ber
std::vector<size_t> flipBer(Bits &bits, double ber, std::mt19937 &rng);

// Gilbert-Elliott bit channel: good state ber, bad state berBad
struct BurstChannel {
    double pGoodToBad = 0.001, pBadToGood = 0.1, berGood = 0.0, berBad = 0.5;
    bool bad = false;
    std::vector<size_t> apply(Bits &bits, std::mt19937 &rng);
};

// parse "3,17,40" into positions
std::vector<size_t> parsePositions(const std::string &s);

}  // namespace edc
