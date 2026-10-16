#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace crackdown {

using Sha256Digest = std::array<uint8_t, 32>;

class Sha256 {
 public:
  Sha256() { Reset(); }

  void Reset() {
    state_ = {0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au, 0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u};
    buffered_ = 0;
    total_bytes_ = 0;
  }

  void Update(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    total_bytes_ += size;
    if (buffered_ > 0) {
      const size_t take = size < kBlockBytes - buffered_ ? size : kBlockBytes - buffered_;
      std::memcpy(buffer_.data() + buffered_, bytes, take);
      buffered_ += take;
      bytes += take;
      size -= take;
      if (buffered_ < kBlockBytes) {
        return;
      }
      Compress(buffer_.data());
      buffered_ = 0;
    }
    for (; size >= kBlockBytes; bytes += kBlockBytes, size -= kBlockBytes) {
      Compress(bytes);
    }
    if (size > 0) {
      std::memcpy(buffer_.data(), bytes, size);
      buffered_ = size;
    }
  }

  // The object must be Reset before it hashes another message.
  Sha256Digest Finish() {
    const uint64_t bit_length = total_bytes_ * 8;
    const uint8_t padding_start = 0x80;
    Update(&padding_start, 1);
    const uint8_t zero = 0;
    while (buffered_ != kBlockBytes - 8) {
      Update(&zero, 1);
    }
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) {
      length[i] = uint8_t(bit_length >> (56 - 8 * i));
    }
    Update(length, sizeof(length));
    Sha256Digest digest;
    for (size_t i = 0; i < state_.size(); ++i) {
      digest[4 * i] = uint8_t(state_[i] >> 24);
      digest[4 * i + 1] = uint8_t(state_[i] >> 16);
      digest[4 * i + 2] = uint8_t(state_[i] >> 8);
      digest[4 * i + 3] = uint8_t(state_[i]);
    }
    return digest;
  }

 private:
  static constexpr size_t kBlockBytes = 64;

  static uint32_t RotateRight(uint32_t value, int count) { return (value >> count) | (value << (32 - count)); }

  void Compress(const uint8_t* block) {
    static constexpr uint32_t kRound[64] = {
        0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
        0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
        0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
        0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
        0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
        0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
        0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
        0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};

    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = uint32_t(block[4 * i]) << 24 | uint32_t(block[4 * i + 1]) << 16 | uint32_t(block[4 * i + 2]) << 8 |
             block[4 * i + 3];
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
      const uint32_t choose = (e & f) ^ (~e & g);
      const uint32_t t1 = h + s1 + choose + kRound[i] + w[i];
      const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<uint32_t, 8> state_;
  std::array<uint8_t, kBlockBytes> buffer_;
  size_t buffered_ = 0;
  uint64_t total_bytes_ = 0;
};

// Upper-case hexadecimal.
inline std::string ToHex(const Sha256Digest& digest) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string out;
  for (const uint8_t byte : digest) {
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 0xF]);
  }
  return out;
}

}  // namespace crackdown
