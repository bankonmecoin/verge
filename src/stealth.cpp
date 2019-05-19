// Copyright (c) 2014 The ShadowCoin developers
// Copyright (c) 2018 Verge
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <stealth.h>
#include <base58.h>
#include <uint256.h>
#include <crypto/sha256.h>
#include <arith_uint256.h>

#include <openssl/rand.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

const uint8_t stealth_version_byte = 0x28;
const uint8_t stealth_dump_version_byte = 0x2b;


bool CStealthAddress::Import(const std::string& stealthSecrets)
{
    data_chunk raw;
    
    if (!DecodeBase58(stealthSecrets, raw))
    {
        LogPrintf("CStealthAddress::Import DecodeBase58 failed.\n");
        return false;
    };
    
    if (!VerifyChecksum(raw))
    {

        LogPrintf("CStealthAddress::Import verify_checksum failed.\n");
        return false;
    };
    
    /*
     * 1. version
     * 2. scan pubkey
     * 3. scan secret
     * 4. spend pubkey
     * 5. spend privkey 
    */ 
    if (raw.size() < 2 + 33 + 32 + 33 + 32)
    {
        LogPrintf("CStealthAddress::Import too few bytes provided.\n");
        return false;
    };
    
    uint8_t* p = &raw[0];
    uint8_t  version = *p++;
    
    if (version != stealth_dump_version_byte)
    {
        LogPrintf("CStealthAddress::Import version mismatch 0x%x != 0x%x.\n", version, stealth_version_byte);
        return false;
    };
    
    scan_pubkey.resize(33);
    memcpy(&scan_pubkey[0], p, 33);
    p += 33;

    scan_secret.resize(32);
    memcpy(&scan_secret[0], p, 32);
    p += 32;
    
    spend_pubkey.resize(33);
    memcpy(&spend_pubkey[0], p, 33);
    p += 33;

    spend_secret.resize(32);
    memcpy(&spend_secret[0], p, 32);
    p += 32;

    return true;
};

bool CStealthAddress::SetEncoded(const std::string& encodedAddress)
{
    data_chunk raw;
    
    if (!DecodeBase58(encodedAddress, raw))
    {
        LogPrintf("CStealthAddress::SetEncoded DecodeBase58 failed.\n");
        return false;
    };
    
    if (!VerifyChecksum(raw))
    {

        LogPrintf("CStealthAddress::SetEncoded verify_checksum failed.\n");
        return false;
    };
    
    if (raw.size() < 1 + 1 + 33 + 1 + 33 + 1 + 1 + 4)
    {
        LogPrintf("CStealthAddress::SetEncoded() too few bytes provided.\n");
        return false;
    };
    
    
    uint8_t* p = &raw[0];
    uint8_t version = *p++;
    
    if (version != stealth_version_byte)
    {
        LogPrintf("CStealthAddress::SetEncoded version mismatch 0x%x != 0x%x.\n", version, stealth_version_byte);
        return false;
    };
    
    options = *p++;
    
    scan_pubkey.resize(33);
    memcpy(&scan_pubkey[0], p, 33);
    p += 33;
    //uint8_t spend_pubkeys = *p++;
    p++;
    
    spend_pubkey.resize(33);
    memcpy(&spend_pubkey[0], p, 33);
    
    return true;
};

std::string CStealthAddress::Encoded() const
{
    // https://wiki.unsystem.net/index.php/DarkWallet/Stealth#Address_format
    // [version] [options] [scan_key] [N] ... [Nsigs] [prefix_length] ...
    
    data_chunk raw;
    raw.push_back(stealth_version_byte);
    
    raw.push_back(options);
    
    raw.insert(raw.end(), scan_pubkey.begin(), scan_pubkey.end());
    raw.push_back(1); // number of spend pubkeys
    raw.insert(raw.end(), spend_pubkey.begin(), spend_pubkey.end());
    raw.push_back(0); // number of signatures
    raw.push_back(0); // ?
    
    AppendChecksum(raw);
    
    return EncodeBase58(raw);
};

std::string CStealthAddress::Export() const
{
    data_chunk raw;

    raw.push_back(stealth_dump_version_byte);
    
    raw.insert(raw.end(), scan_pubkey.begin(), scan_pubkey.end());
    raw.insert(raw.end(), scan_secret.begin(), scan_secret.end());
    raw.insert(raw.end(), spend_pubkey.begin(), spend_pubkey.end());
    raw.insert(raw.end(), spend_secret.begin(), spend_secret.end());

    AppendChecksum(raw);
    
    return EncodeBase58(raw);
};

uint32_t BitcoinChecksum(uint8_t* p, uint32_t nBytes)
{
    if (!p || nBytes == 0)
        return 0;
    
    uint8_t hash1[32];
    CSHA256().Write(p, nBytes).Finalize((uint8_t*)hash1);
    // SHA256(p, nBytes, (uint8_t*)hash1);

    uint8_t hash2[32];
    CSHA256().Write((uint8_t*)hash1, sizeof(hash1)).Finalize((uint8_t*)hash2);
    // SHA256((uint8_t*)hash1, sizeof(hash1), (uint8_t*)hash2);
    
    // -- checksum is the 1st 4 bytes of the hash
    uint32_t checksum = from_little_endian<uint32_t>(&hash2[0]);
    
    return checksum;
};

void AppendChecksum(data_chunk& data)
{
    uint32_t checksum = BitcoinChecksum(&data[0], data.size());
    
    // -- to_little_endian
    std::vector<uint8_t> tmp(4);
    
    //memcpy(&tmp[0], &checksum, 4);
    for (int i = 0; i < 4; ++i)
    {
        tmp[i] = checksum & 0xFF;
        checksum >>= 8;
    };
    
    data.insert(data.end(), tmp.begin(), tmp.end());
};

bool VerifyChecksum(const data_chunk& data)
{
    if (data.size() < 4)
        return false;
    
    uint32_t checksum = from_little_endian<uint32_t>(data.end() - 4);
    
    return BitcoinChecksum((uint8_t*)&data[0], data.size()-4) == checksum;
};


int GenerateRandomSecret(ec_secret& out)
{
    RandAddSeedPerfmon();
    
    static arith_uint256 max = arith_uint256("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364140");
    static arith_uint256 min = arith_uint256(16000); // increase? min valid key is 1
    
    uint256 test;
    
    int i;
    // -- check max, try max 32 times
    for (i = 0; i < 32; ++i)
    {
        RAND_bytes((unsigned char*) test.begin(), 32);
        if (UintToArith256(test) > min && UintToArith256(test) < max)
        {
            memcpy(&out.e[0], test.begin(), 32);
            break;
        };
    };
    
    if (i > 31)
    {
        printf("Error: GenerateRandomSecret failed to generate a valid key.\n");
        return 1;
    };
    
    return 0;
};

int SecretToPublicKey(const ec_secret& secret, ec_point& out)
{
    // -- public key = private * G
    int rv = 0;
    
    EC_GROUP *ecgrp = EC_GROUP_new_by_curve_name(NID_secp256k1);
    
    if (!ecgrp)
    {
        printf("SecretToPublicKey(): EC_GROUP_new_by_curve_name failed.\n");
        return 1;
    };

    BIGNUM* bnIn = BN_bin2bn(&secret.e[0], ec_secret_size, BN_new());
    if (!bnIn)
    {
        EC_GROUP_free(ecgrp);
        printf("SecretToPublicKey(): BN_bin2bn failed\n");
        return 1;
    };
    
    EC_POINT* pub = EC_POINT_new(ecgrp);
    
    
    EC_POINT_mul(ecgrp, pub, bnIn, NULL, NULL, NULL);
    
    BIGNUM* bnOut = EC_POINT_point2bn(ecgrp, pub, POINT_CONVERSION_COMPRESSED, BN_new(), NULL);
    if (!bnOut)
    {
        printf("SecretToPublicKey(): point2bn failed\n");
        rv = 1;
    } else
    {
        out.resize(ec_compressed_size);
        if (BN_num_bytes(bnOut) != (int) ec_compressed_size
            || BN_bn2bin(bnOut, &out[0]) != (int) ec_compressed_size)
        {
            printf("SecretToPublicKey(): bnOut incorrect length.\n");
            rv = 1;
        };
        
        BN_free(bnOut);
    };
    
    EC_GROUP_free(ecgrp);
    BN_free(bnIn);
    EC_POINT_free(pub);

    return rv;
};


int StealthSecret(ec_secret& secret, const ec_point& pubkey, const ec_point& pkSpend, ec_secret& sharedSOut, ec_point& pkOut)
{
    /*
    
    send:
        secret = ephem_secret, pubkey = scan_pubkey
    
    receive:
        secret = scan_secret, pubkey = ephem_pubkey
        c = H(dP)
    
    Q = public scan key (EC point, 33 bytes)
    d = private scan key (integer, 32 bytes)
    R = public spend key
    f = private spend key

    Q = dG
    R = fG
    
    Sender (has Q and R, not d or f):
    
    P = eG

    c = H(eQ) = H(dP)
    R' = R + cG
    
    
    Recipient gets R' and P
    
    test 0 and infinity?
    */
    
    int rv = 0;
    std::vector<uint8_t> vchOutQ;
    
    BN_CTX* bnCtx   = NULL;
    BIGNUM* bnEphem = NULL;
    BIGNUM* bnQ     = NULL;
    EC_POINT* Q     = NULL;
    BIGNUM* bnOutQ  = NULL;
    BIGNUM* bnc     = NULL;
    EC_POINT* C     = NULL;
    BIGNUM* bnR     = NULL;
    EC_POINT* R     = NULL;
    EC_POINT* Rout  = NULL;
    BIGNUM* bnOutR  = NULL;
    
    EC_GROUP* ecgrp = EC_GROUP_new_by_curve_name(NID_secp256k1);
    
    if (!ecgrp)
    {
        printf("StealthSecret(): EC_GROUP_new_by_curve_name failed.\n");
        return 1;
    };
    
    if (!(bnCtx = BN_CTX_new()))
    {
        printf("StealthSecret(): BN_CTX_new failed.\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnEphem = BN_bin2bn(&secret.e[0], ec_secret_size, BN_new())))
    {
        printf("StealthSecret(): bnEphem BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnQ = BN_bin2bn(&pubkey[0], pubkey.size(), BN_new())))
    {
        printf("StealthSecret(): bnQ BN_bin2bn failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(Q = EC_POINT_bn2point(ecgrp, bnQ, NULL, bnCtx)))
    {
        printf("StealthSecret(): Q EC_POINT_bn2point failed\n");
        rv = 1;
        goto End;
    };
    
    // -- eQ
    // EC_POINT_mul(const EC_GROUP *group, EC_POINT *r, const BIGNUM *n, const EC_POINT *q, const BIGNUM *m, BN_CTX *ctx);
    // EC_POINT_mul calculates the value generator * n + q * m and stores the result in r. The value n may be NULL in which case the result is just q * m. 
    if (!EC_POINT_mul(ecgrp, Q, NULL, Q, bnEphem, bnCtx))
    {
        printf("StealthSecret(): eQ EC_POINT_mul failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnOutQ = EC_POINT_point2bn(ecgrp, Q, POINT_CONVERSION_COMPRESSED, BN_new(), bnCtx)))
    {
        printf("StealthSecret(): Q EC_POINT_bn2point failed\n");
        rv = 1;
        goto End;
    };
    
    
    vchOutQ.resize(ec_compressed_size);
    if (BN_num_bytes(bnOutQ) != (int) ec_compressed_size
        || BN_bn2bin(bnOutQ, &vchOutQ[0]) != (int) ec_compressed_size)
    {
        printf("StealthSecret(): bnOutQ incorrect length.\n");
        rv = 1;
        goto End;
    };
    
    // SHA256(&vchOutQ[0], vchOutQ.size(), &sharedSOut.e[0]);
    CSHA256().Write(&vchOutQ[0], vchOutQ.size()).Finalize(&sharedSOut.e[0]);
    
    if (!(bnc = BN_bin2bn(&sharedSOut.e[0], ec_secret_size, BN_new())))
    {
        printf("StealthSecret(): BN_bin2bn failed\n");
        rv = 1;
        goto End;
    };
    
    // -- cG
    if (!(C = EC_POINT_new(ecgrp)))
    {
        printf("StealthSecret(): C EC_POINT_new failed\n");
        rv = 1;
        goto End;
    };
    
    if (!EC_POINT_mul(ecgrp, C, bnc, NULL, NULL, bnCtx))
    {
        printf("StealthSecret(): C EC_POINT_mul failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnR = BN_bin2bn(&pkSpend[0], pkSpend.size(), BN_new())))
    {
        printf("StealthSecret(): bnR BN_bin2bn failed\n");
        rv = 1;
        goto End;
    };
    
    
    if (!(R = EC_POINT_bn2point(ecgrp, bnR, NULL, bnCtx)))
    {
        printf("StealthSecret(): R EC_POINT_bn2point failed\n");
        rv = 1;
        goto End;
    };
    
    if (!EC_POINT_mul(ecgrp, C, bnc, NULL, NULL, bnCtx))
    {
        printf("StealthSecret(): C EC_POINT_mul failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(Rout = EC_POINT_new(ecgrp)))
    {
        printf("StealthSecret(): Rout EC_POINT_new failed\n");
        rv = 1;
        goto End;
    };
    
    if (!EC_POINT_add(ecgrp, Rout, R, C, bnCtx))
    {
        printf("StealthSecret(): Rout EC_POINT_add failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnOutR = EC_POINT_point2bn(ecgrp, Rout, POINT_CONVERSION_COMPRESSED, BN_new(), bnCtx)))
    {
        printf("StealthSecret(): Rout EC_POINT_bn2point failed\n");
        rv = 1;
        goto End;
    };
    
    
    pkOut.resize(ec_compressed_size);
    if (BN_num_bytes(bnOutR) != (int) ec_compressed_size
        || BN_bn2bin(bnOutR, &pkOut[0]) != (int) ec_compressed_size)
    {
        printf("StealthSecret(): pkOut incorrect length.\n");
        rv = 1;
        goto End;
    };
    
    End:
    if (bnOutR)     BN_free(bnOutR);
    if (Rout)       EC_POINT_free(Rout);
    if (R)          EC_POINT_free(R);
    if (bnR)        BN_free(bnR);
    if (C)          EC_POINT_free(C);
    if (bnc)        BN_free(bnc);
    if (bnOutQ)     BN_free(bnOutQ);
    if (Q)          EC_POINT_free(Q);
    if (bnQ)        BN_free(bnQ);
    if (bnEphem)    BN_free(bnEphem);
    if (bnCtx)      BN_CTX_free(bnCtx);
    EC_GROUP_free(ecgrp);
    
    return rv;
};


int StealthSecretSpend(ec_secret& scanSecret, ec_point& ephemPubkey, ec_secret& spendSecret, ec_secret& secretOut)
{
    /*
    
    c  = H(dP)
    R' = R + cG     [without decrypting wallet]
       = (f + c)G   [after decryption of wallet]
         Remember: mod curve.order, pad with 0x00s where necessary?
    */
    
    int rv = 0;
    std::vector<uint8_t> vchOutP;
    
    BN_CTX* bnCtx           = NULL;
    BIGNUM* bnScanSecret    = NULL;
    BIGNUM* bnP             = NULL;
    EC_POINT* P             = NULL;
    BIGNUM* bnOutP          = NULL;
    BIGNUM* bnc             = NULL;
    BIGNUM* bnOrder         = NULL;
    BIGNUM* bnSpend         = NULL;
    
    EC_GROUP* ecgrp = EC_GROUP_new_by_curve_name(NID_secp256k1);
    
    if (!ecgrp)
    {
        printf("StealthSecretSpend(): EC_GROUP_new_by_curve_name failed.\n");
        return 1;
    };
    
    if (!(bnCtx = BN_CTX_new()))
    {
        printf("StealthSecretSpend(): BN_CTX_new failed.\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnScanSecret = BN_bin2bn(&scanSecret.e[0], ec_secret_size, BN_new())))
    {
        printf("StealthSecretSpend(): bnScanSecret BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnP = BN_bin2bn(&ephemPubkey[0], ephemPubkey.size(), BN_new())))
    {
        printf("StealthSecretSpend(): bnP BN_bin2bn failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(P = EC_POINT_bn2point(ecgrp, bnP, NULL, bnCtx)))
    {
        printf("StealthSecretSpend(): P EC_POINT_bn2point failed\n");
        rv = 1;
        goto End;
    };
    
    // -- dP
    if (!EC_POINT_mul(ecgrp, P, NULL, P, bnScanSecret, bnCtx))
    {
        printf("StealthSecretSpend(): dP EC_POINT_mul failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnOutP = EC_POINT_point2bn(ecgrp, P, POINT_CONVERSION_COMPRESSED, BN_new(), bnCtx)))
    {
        printf("StealthSecretSpend(): P EC_POINT_bn2point failed\n");
        rv = 1;
        goto End;
    };
    
    
    vchOutP.resize(ec_compressed_size);
    if (BN_num_bytes(bnOutP) != (int) ec_compressed_size
        || BN_bn2bin(bnOutP, &vchOutP[0]) != (int) ec_compressed_size)
    {
        printf("StealthSecretSpend(): bnOutP incorrect length.\n");
        rv = 1;
        goto End;
    };
    
    uint8_t hash1[32];
    CSHA256().Write(&vchOutP[0], vchOutP.size()).Finalize((uint8_t*)hash1);
    // SHA256(&vchOutP[0], vchOutP.size(), (uint8_t*)hash1);
    
    
    if (!(bnc = BN_bin2bn(&hash1[0], 32, BN_new())))
    {
        printf("StealthSecretSpend(): BN_bin2bn failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnOrder = BN_new())
        || !EC_GROUP_get_order(ecgrp, bnOrder, bnCtx))
    {
        printf("StealthSecretSpend(): EC_GROUP_get_order failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnSpend = BN_bin2bn(&spendSecret.e[0], ec_secret_size, BN_new())))
    {
        printf("StealthSecretSpend(): bnSpend BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };
    
    //if (!BN_add(r, a, b)) return 0;
    //return BN_nnmod(r, r, m, ctx);
    if (!BN_mod_add(bnSpend, bnSpend, bnc, bnOrder, bnCtx))
    {
        printf("StealthSecretSpend(): bnSpend BN_mod_add failed.\n");
        rv = 1;
        goto End;
    };
    
    if (BN_is_zero(bnSpend)) // possible?
    {
        printf("StealthSecretSpend(): bnSpend is zero.\n");
        rv = 1;
        goto End;
    };
    
    if (BN_num_bytes(bnSpend) != (int) ec_secret_size
        || BN_bn2bin(bnSpend, &secretOut.e[0]) != (int) ec_secret_size)
    {
        printf("StealthSecretSpend(): bnSpend incorrect length.\n");
        rv = 1;
        goto End;
    };
    
    End:
    if (bnSpend)        BN_free(bnSpend);
    if (bnOrder)        BN_free(bnOrder);
    if (bnc)            BN_free(bnc);
    if (bnOutP)         BN_free(bnOutP);
    if (P)              EC_POINT_free(P);
    if (bnP)            BN_free(bnP);
    if (bnScanSecret)   BN_free(bnScanSecret);
    if (bnCtx)          BN_CTX_free(bnCtx);
    EC_GROUP_free(ecgrp);
    
    return rv;
};


int StealthSharedToSecretSpend(ec_secret& sharedS, ec_secret& spendSecret, ec_secret& secretOut)
{
    
    int rv = 0;
    std::vector<uint8_t> vchOutP;
    
    BN_CTX* bnCtx           = NULL;
    BIGNUM* bnc             = NULL;
    BIGNUM* bnOrder         = NULL;
    BIGNUM* bnSpend         = NULL;
    
    EC_GROUP* ecgrp = EC_GROUP_new_by_curve_name(NID_secp256k1);
    
    if (!ecgrp)
    {
        printf("StealthSecretSpend(): EC_GROUP_new_by_curve_name failed.\n");
        return 1;
    };
    
    if (!(bnCtx = BN_CTX_new()))
    {
        printf("StealthSecretSpend(): BN_CTX_new failed.\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnc = BN_bin2bn(&sharedS.e[0], ec_secret_size, BN_new())))
    {
        printf("StealthSecretSpend(): BN_bin2bn failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnOrder = BN_new())
        || !EC_GROUP_get_order(ecgrp, bnOrder, bnCtx))
    {
        printf("StealthSecretSpend(): EC_GROUP_get_order failed\n");
        rv = 1;
        goto End;
    };
    
    if (!(bnSpend = BN_bin2bn(&spendSecret.e[0], ec_secret_size, BN_new())))
    {
        printf("StealthSecretSpend(): bnSpend BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };
    
    //if (!BN_add(r, a, b)) return 0;
    //return BN_nnmod(r, r, m, ctx);
    if (!BN_mod_add(bnSpend, bnSpend, bnc, bnOrder, bnCtx))
    {
        printf("StealthSecretSpend(): bnSpend BN_mod_add failed.\n");
        rv = 1;
        goto End;
    };
    
    if (BN_is_zero(bnSpend)) // possible?
    {
        printf("StealthSecretSpend(): bnSpend is zero.\n");
        rv = 1;
        goto End;
    };
    
    if (BN_num_bytes(bnSpend) != (int) ec_secret_size
        || BN_bn2bin(bnSpend, &secretOut.e[0]) != (int) ec_secret_size)
    {
        printf("StealthSecretSpend(): bnSpend incorrect length.\n");
        rv = 1;
        goto End;
    };
    
    End:
    if (bnSpend)        BN_free(bnSpend);
    if (bnOrder)        BN_free(bnOrder);
    if (bnc)            BN_free(bnc);
    if (bnCtx)          BN_CTX_free(bnCtx);
    EC_GROUP_free(ecgrp);
    
    return rv;
};

bool IsStealthAddress(const std::string& encodedAddress)
{
    data_chunk raw;
    
    if (!DecodeBase58(encodedAddress, raw))
    {
        //printf("IsStealthAddress DecodeBase58 failed.\n");
        return false;
    };
    
    if (!VerifyChecksum(raw))
    {
        //printf("IsStealthAddress verify_checksum failed.\n");
        return false;
    };
    
    if (raw.size() < 1 + 1 + 33 + 1 + 33 + 1 + 1 + 4)
    {
        //printf("IsStealthAddress too few bytes provided.\n");
        return false;
    };
    
    
    uint8_t* p = &raw[0];
    uint8_t version = *p++;
    
    if (version != stealth_version_byte)
    {
        //printf("IsStealthAddress version mismatch 0x%x != 0x%x.\n", version, stealth_version_byte);
        return false;
    };
    
    return true;
};

bool GenerateNewStealthAddress(std::string& sError, std::string& sLabel, CStealthAddress& sxAddr) {
    ec_secret scan_secret;
    ec_secret spend_secret;
    
    if (GenerateRandomSecret(scan_secret) != 0
        || GenerateRandomSecret(spend_secret) != 0)
    {
        sError = "GenerateRandomSecret failed.";
        LogPrintf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
        return false;
    };
    
    ec_point scan_pubkey, spend_pubkey;
    if (SecretToPublicKey(scan_secret, scan_pubkey) != 0)
    {
        sError = "Could not get scan public key.";
        LogPrintf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
        return false;
    };
    
    if (SecretToPublicKey(spend_secret, spend_pubkey) != 0)
    {
        sError = "Could not get spend public key.";
        LogPrintf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
        return false;
    };
    
    // leaving  Log Prints for debugging reasons.
    LogPrintf("getnewstealthaddress: \n");
    LogPrintf("scan_pubkey \n");
    for (uint32_t i = 0; i < scan_pubkey.size(); ++i)
        LogPrintf("%02x\n", scan_pubkey[i]);
    LogPrintf("\n");
    
    LogPrintf("spend_pubkey \n");
    for (uint32_t i = 0; i < spend_pubkey.size(); ++i)
        LogPrintf("%02x\n", spend_pubkey[i]);
    LogPrintf("\n");
    
    sxAddr.label = sLabel;
    sxAddr.scan_pubkey = scan_pubkey;
    sxAddr.spend_pubkey = spend_pubkey;
    
    sxAddr.scan_secret.resize(32);
    memcpy(&sxAddr.scan_secret[0], &scan_secret.e[0], 32);
    sxAddr.spend_secret.resize(32);
    memcpy(&sxAddr.spend_secret[0], &spend_secret.e[0], 32);
    
    return true;
}