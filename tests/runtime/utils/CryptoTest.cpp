#include <doctest/doctest.h>

#include <Windows.h>
#include <bcrypt.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "utils/crypto.h"

using d2bs::utils::HashFile;
using d2bs::utils::HashString;

TEST_CASE("HashString matches the FIPS / RFC test vectors") {
    CHECK(HashString(BCRYPT_MD5_ALGORITHM, "abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(HashString(BCRYPT_SHA1_ALGORITHM, "abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(HashString(BCRYPT_SHA256_ALGORITHM, "abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(HashString(BCRYPT_SHA384_ALGORITHM, "abc") ==
          "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
    CHECK(HashString(BCRYPT_SHA512_ALGORITHM, "abc") ==
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d44236"
          "43ce80e2a9ac94fa54ca49f");
    CHECK(HashString(BCRYPT_MD5_ALGORITHM, "") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(HashString(BCRYPT_SHA256_ALGORITHM, "") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("HashFile hashes a file's exact bytes") {
    const auto path = std::filesystem::temp_directory_path() / "d2bs_crypto_test.bin";
    // CRLF line endings and a long run, so text-mode translation or a single short read would show.
    std::string bytes = "a\r\nb\r\n";
    bytes += std::string(200 * 1024, 'x');
    {
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    CHECK(HashFile(BCRYPT_MD5_ALGORITHM, path) == HashString(BCRYPT_MD5_ALGORITHM, bytes));
    CHECK(HashFile(BCRYPT_SHA512_ALGORITHM, path) == HashString(BCRYPT_SHA512_ALGORITHM, bytes));
    std::filesystem::remove(path);

    CHECK(HashFile(BCRYPT_MD5_ALGORITHM, path).empty());
}
