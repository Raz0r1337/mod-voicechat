/*
 * mod-voicechat - OCB2-AES128 (see OcbCrypt.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/OcbCrypt.h"

#include <mbedtls/aes.h>

#include <cstring>

namespace voicecore
{
    struct OcbCrypt::Impl
    {
        mbedtls_aes_context enc;
        mbedtls_aes_context dec;
        Impl() { mbedtls_aes_init(&enc); mbedtls_aes_init(&dec); }
        ~Impl() { mbedtls_aes_free(&enc); mbedtls_aes_free(&dec); }
    };

    namespace
    {
        using Block = uint8_t[OcbCrypt::BLOCK];

        // DE: Verdopplung in GF(2^128), Big-Endian. EN: doubling in GF(2^128), big endian.
        void S2(uint8_t* b)
        {
            uint8_t carry = b[0] >> 7;
            for (size_t i = 0; i < 15; ++i)
                b[i] = uint8_t((b[i] << 1) | (b[i + 1] >> 7));
            b[15] = uint8_t((b[15] << 1) ^ (carry * 0x87));
        }

        // DE: Verdreifachung: b ^= 2b. EN: tripling: b ^= 2b.
        void S3(uint8_t* b)
        {
            Block t;
            std::memcpy(t, b, 16);
            S2(t);
            for (size_t i = 0; i < 16; ++i) b[i] ^= t[i];
        }

        void Xor(uint8_t* dst, const uint8_t* a, const uint8_t* b)
        {
            for (size_t i = 0; i < 16; ++i) dst[i] = a[i] ^ b[i];
        }
    }

    OcbCrypt::OcbCrypt() : _impl(std::make_unique<Impl>()) { }
    OcbCrypt::~OcbCrypt() = default;

    void OcbCrypt::AesEnc(const uint8_t* in, uint8_t* out) { mbedtls_aes_crypt_ecb(&_impl->enc, MBEDTLS_AES_ENCRYPT, in, out); }
    void OcbCrypt::AesDec(const uint8_t* in, uint8_t* out) { mbedtls_aes_crypt_ecb(&_impl->dec, MBEDTLS_AES_DECRYPT, in, out); }

    bool OcbCrypt::SetKey(const std::string& key, const std::string& encryptIv, const std::string& decryptIv)
    {
        if (key.size() != 16 || encryptIv.size() != BLOCK || decryptIv.size() != BLOCK)
            return false;
        const auto* k = reinterpret_cast<const unsigned char*>(key.data());
        if (mbedtls_aes_setkey_enc(&_impl->enc, k, 128) != 0 || mbedtls_aes_setkey_dec(&_impl->dec, k, 128) != 0)
            return false;
        std::memcpy(_encIv, encryptIv.data(), BLOCK);
        std::memcpy(_decIv, decryptIv.data(), BLOCK);
        std::memset(_history, 0, sizeof(_history));
        _valid = true;
        return true;
    }

    bool OcbCrypt::SetDecryptIv(const std::string& iv)
    {
        if (iv.size() != BLOCK) return false;
        std::memcpy(_decIv, iv.data(), BLOCK);
        return true;
    }

    std::string OcbCrypt::EncryptIv() const { return std::string(reinterpret_cast<const char*>(_encIv), BLOCK); }

    bool OcbCrypt::Encrypt(const uint8_t* src, uint8_t* dst, size_t len)
    {
        if (!_valid) return false;
        uint8_t tag[BLOCK];
        for (size_t i = 0; i < BLOCK; ++i)
            if (++_encIv[i]) break;
        if (!OcbEncrypt(src, dst + OVERHEAD, len, _encIv, tag))
            return false;
        dst[0] = _encIv[0];
        dst[1] = tag[0];
        dst[2] = tag[1];
        dst[3] = tag[2];
        return true;
    }

    bool OcbCrypt::Decrypt(const uint8_t* src, uint8_t* dst, size_t cryptedLen)
    {
        if (!_valid || cryptedLen < OVERHEAD) return false;
        size_t plainLen = cryptedLen - OVERHEAD;
        uint8_t saveIv[BLOCK];
        uint8_t ivByte = src[0];
        bool restore = false;
        uint8_t tag[BLOCK];
        int lostNow = 0, lateNow = 0;

        std::memcpy(saveIv, _decIv, BLOCK);

        // DE: Logik 1:1 wie Mumble (Reihenfolge/Verlust/Replay). EN: logic 1:1 as in Mumble.
        if (uint8_t(_decIv[0] + 1) == ivByte)
        {
            if (ivByte > _decIv[0])
                _decIv[0] = ivByte;
            else if (ivByte < _decIv[0])
            {
                _decIv[0] = ivByte;
                for (size_t i = 1; i < BLOCK; ++i)
                    if (++_decIv[i]) break;
            }
            else
                return false;
        }
        else
        {
            int diff = int(ivByte) - int(_decIv[0]);
            if (diff > 128) diff -= 256;
            else if (diff < -128) diff += 256;

            if (ivByte < _decIv[0] && diff > -30 && diff < 0)
            {
                lateNow = 1; lostNow = -1;
                _decIv[0] = ivByte;
                restore = true;
            }
            else if (ivByte > _decIv[0] && diff > -30 && diff < 0)
            {
                lateNow = 1; lostNow = -1;
                _decIv[0] = ivByte;
                for (size_t i = 1; i < BLOCK; ++i)
                    if (_decIv[i]--) break;
                restore = true;
            }
            else if (ivByte > _decIv[0] && diff > 0)
            {
                lostNow = ivByte - _decIv[0] - 1;
                _decIv[0] = ivByte;
            }
            else if (ivByte < _decIv[0] && diff > 0)
            {
                lostNow = 256 - _decIv[0] + ivByte - 1;
                _decIv[0] = ivByte;
                for (size_t i = 1; i < BLOCK; ++i)
                    if (++_decIv[i]) break;
            }
            else
                return false;

            if (_history[_decIv[0]] == _decIv[1])
            {
                std::memcpy(_decIv, saveIv, BLOCK);
                return false;
            }
        }

        bool ok = OcbDecrypt(src + OVERHEAD, dst, plainLen, _decIv, tag);
        if (!ok || std::memcmp(tag, src + 1, 3) != 0)
        {
            std::memcpy(_decIv, saveIv, BLOCK);
            return false;
        }
        _history[_decIv[0]] = _decIv[1];

        if (restore)
            std::memcpy(_decIv, saveIv, BLOCK);

        ++good;
        if (lateNow > 0) late += uint32_t(lateNow);
        if (lostNow > 0) lost += uint32_t(lostNow);
        else if (lostNow < 0 && lost > 0) --lost;
        return true;
    }

    bool OcbCrypt::OcbEncrypt(const uint8_t* plain, uint8_t* enc, size_t len, const uint8_t* nonce, uint8_t* tag)
    {
        Block checksum = {}, delta, tmp, pad;
        AesEnc(nonce, delta);

        while (len > BLOCK)
        {
            // DE: XEX*-Gegenmassnahme wie in Mumble. EN: XEX* countermeasure as in Mumble.
            bool flip = false;
            if (len - BLOCK <= BLOCK)
            {
                uint8_t sum = 0;
                for (size_t i = 0; i < BLOCK - 1; ++i) sum |= plain[i];
                flip = (sum == 0);
            }
            S2(delta);
            Xor(tmp, delta, plain);
            if (flip) tmp[0] ^= 1;
            AesEnc(tmp, tmp);
            Xor(enc, delta, tmp);
            Xor(checksum, checksum, plain);
            if (flip) checksum[0] ^= 1;
            len -= BLOCK; plain += BLOCK; enc += BLOCK;
        }

        S2(delta);
        std::memset(tmp, 0, BLOCK);
        tmp[BLOCK - 1] = uint8_t(len * 8);
        Xor(tmp, tmp, delta);
        AesEnc(tmp, pad);
        std::memcpy(tmp, plain, len);
        std::memcpy(tmp + len, pad + len, BLOCK - len);
        Xor(checksum, checksum, tmp);
        Xor(tmp, pad, tmp);
        std::memcpy(enc, tmp, len);

        S3(delta);
        Xor(tmp, delta, checksum);
        AesEnc(tmp, tag);
        return true;
    }

    bool OcbCrypt::OcbDecrypt(const uint8_t* enc, uint8_t* plain, size_t len, const uint8_t* nonce, uint8_t* tag)
    {
        Block checksum = {}, delta, tmp, pad;
        bool ok = true;
        AesEnc(nonce, delta);

        while (len > BLOCK)
        {
            S2(delta);
            Xor(tmp, delta, enc);
            AesDec(tmp, tmp);
            Xor(plain, delta, tmp);
            Xor(checksum, checksum, plain);
            len -= BLOCK; plain += BLOCK; enc += BLOCK;
        }

        S2(delta);
        std::memset(tmp, 0, BLOCK);
        tmp[BLOCK - 1] = uint8_t(len * 8);
        Xor(tmp, tmp, delta);
        AesEnc(tmp, pad);
        std::memset(tmp, 0, BLOCK);
        std::memcpy(tmp, enc, len);
        Xor(tmp, tmp, pad);
        Xor(checksum, checksum, tmp);
        std::memcpy(plain, tmp, len);

        if (std::memcmp(tmp, delta, BLOCK - 1) == 0)
            ok = false;

        S3(delta);
        Xor(tmp, delta, checksum);
        AesEnc(tmp, tag);
        return ok;
    }
}
