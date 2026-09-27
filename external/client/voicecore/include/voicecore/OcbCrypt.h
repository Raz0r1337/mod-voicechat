/*
 * mod-voicechat - OCB2-AES128 for Mumble UDP voice (compatible with Mumble's CryptStateOCB2)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DE: Nachbau von Mumbles CryptStateOCB2 inkl. Gegenmassnahme aus
 *     https://eprint.iacr.org/2019/311 (Abschnitt 9). Paketformat:
 *     [iv0][tag0][tag1][tag2][Chiffretext].
 * EN: Re-implementation of Mumble's CryptStateOCB2 incl. the countermeasure
 *     from https://eprint.iacr.org/2019/311 (section 9). Packet format:
 *     [iv0][tag0][tag1][tag2][ciphertext].
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace voicecore
{
    class OcbCrypt
    {
    public:
        static constexpr size_t BLOCK = 16;
        static constexpr size_t OVERHEAD = 4;

        OcbCrypt();
        ~OcbCrypt();

        bool SetKey(const std::string& key, const std::string& encryptIv, const std::string& decryptIv);
        bool SetDecryptIv(const std::string& iv);
        std::string EncryptIv() const;
        bool IsValid() const { return _valid; }

        // DE: dst braucht len + 4 Byte. EN: dst needs len + 4 bytes.
        bool Encrypt(const uint8_t* src, uint8_t* dst, size_t len);
        // DE: dst braucht len - 4 Byte. EN: dst needs len - 4 bytes.
        bool Decrypt(const uint8_t* src, uint8_t* dst, size_t len);

        uint32_t good = 0, late = 0, lost = 0;

    private:
        bool OcbEncrypt(const uint8_t* plain, uint8_t* enc, size_t len, const uint8_t* nonce, uint8_t* tag);
        bool OcbDecrypt(const uint8_t* enc, uint8_t* plain, size_t len, const uint8_t* nonce, uint8_t* tag);
        void AesEnc(const uint8_t* in, uint8_t* out);
        void AesDec(const uint8_t* in, uint8_t* out);

        struct Impl;
        std::unique_ptr<Impl> _impl;
        uint8_t _encIv[BLOCK] = {};
        uint8_t _decIv[BLOCK] = {};
        uint8_t _history[256] = {};
        bool _valid = false;
    };
}
