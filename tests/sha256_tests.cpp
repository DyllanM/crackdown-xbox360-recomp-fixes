// SHA-256 against the NIST vectors, with the message split at every position.

#include <cstdio>
#include <string>
#include <vector>

#include "sha256.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string Hash(const std::string& message) {
  crackdown::Sha256 hash;
  hash.Update(message.data(), message.size());
  return crackdown::ToHex(hash.Finish());
}

void TestVectors() {
  std::printf("NIST vectors\n");
  CHECK(Hash("") == "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855");
  CHECK(Hash("abc") == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
  CHECK(Hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1");
  CHECK(Hash(std::string(1000000, 'a')) == "CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0");
}

void TestStreaming() {
  std::printf("streaming updates match a single update\n");
  std::string message;
  for (int i = 0; i < 300; ++i) {
    message.push_back(char('a' + i % 23));
  }
  const std::string expected = Hash(message);
  for (size_t split = 0; split <= message.size(); ++split) {
    crackdown::Sha256 hash;
    hash.Update(message.data(), split);
    hash.Update(message.data() + split, message.size() - split);
    CHECK(crackdown::ToHex(hash.Finish()) == expected);
  }
  crackdown::Sha256 bytewise;
  for (const char c : message) {
    bytewise.Update(&c, 1);
  }
  CHECK(crackdown::ToHex(bytewise.Finish()) == expected);
}

void TestPaddingBoundaries() {
  std::printf("messages around the block and padding boundaries agree with a one-byte-at-a-time hash\n");
  for (size_t size = 0; size <= 130; ++size) {
    const std::string message(size, 'x');
    crackdown::Sha256 bytewise;
    for (const char c : message) {
      bytewise.Update(&c, 1);
    }
    CHECK(crackdown::ToHex(bytewise.Finish()) == Hash(message));
  }
  CHECK(Hash(std::string(55, 'a')) == "9F4390F8D30C2DD92EC9F095B65E2B9AE9B0A925A5258E241C9F1E910F734318");
  CHECK(Hash(std::string(56, 'a')) == "B35439A4AC6F0948B6D6F9E3C6AF0F5F590CE20F1BDE7090EF7970686EC6738A");
  CHECK(Hash(std::string(64, 'a')) == "FFE054FE7AE0CB6DC65C3AF9B61D5209F439851DB43D0BA5997337DF154668EB");
}

void TestReset() {
  std::printf("a reset hasher starts over\n");
  crackdown::Sha256 hash;
  hash.Update("garbage", 7);
  hash.Reset();
  hash.Update("abc", 3);
  CHECK(crackdown::ToHex(hash.Finish()) == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
  hash.Reset();
  CHECK(crackdown::ToHex(hash.Finish()) == "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855");
}

}  // namespace

int main() {
  TestVectors();
  TestStreaming();
  TestPaddingBoundaries();
  TestReset();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
