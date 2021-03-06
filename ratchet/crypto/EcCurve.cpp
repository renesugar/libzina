/*
Copyright 2016 Silent Circle, LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include <cryptcommon/ZrtpRandom.h>

#include "EcCurve.h"
#include "Ec255PublicKey.h"
#include "../../Constants.h"
#include "../../logging/ZinaLogging.h"

using namespace zina;

static void ecGenerateRandomNumber25519(uint8_t* outBuffer)
{
    LOGGER(DEBUGGING, __func__, " -->");
    unsigned char random[Ec255PrivateKey::KEY_LENGTH];
    ZrtpRandom::getRandomData(random, Ec255PrivateKey::KEY_LENGTH);

    // Same as in curve25519_donna, thus a no-op there if this function generates the secret.
    random[0] &= 248;
    random[31] &= 127;
    random[31] |= 64;

    memcpy(outBuffer, random, Ec255PrivateKey::KEY_LENGTH);
    LOGGER(DEBUGGING, __func__, " <--");
    return;
}

KeyPairUnique EcCurve::generateKeyPair(int32_t curveType)
{
    LOGGER(DEBUGGING, __func__, " -->");
    if (curveType == EcCurveTypes::Curve25519) {
        uint8_t privateKeyData[Ec255PrivateKey::KEY_LENGTH];
        ecGenerateRandomNumber25519(privateKeyData);    // get some random data for private key

        Ec255PrivateKey ecPrivate(privateKeyData);

        // Compute the public key: needs the curve's basepoint
        uint8_t basePoint[Ec255PublicKey::KEY_LENGTH] = {EcCurveTypes::Curve25519Basepoint};
        uint8_t publicKeyData[Ec255PublicKey::KEY_LENGTH];

        curve25519_donna(publicKeyData, privateKeyData, basePoint);
        Ec255PublicKey ecPublic(publicKeyData);
        memset(privateKeyData, 0, Ec255PrivateKey::KEY_LENGTH);  // clear temporary buffer

        KeyPairUnique ecPair = KeyPairUnique(new DhKeyPair(ecPublic, ecPrivate));
        LOGGER(DEBUGGING, __func__, " <--");
        return ecPair;
    }
    LOGGER(WARNING, __func__, " <-- unsupported curve type");
    return NULL;
}


int32_t EcCurve::calculateAgreement(const DhPublicKey& publicKey, const DhPrivateKey& privateKey, uint8_t* agreement, size_t length )
{
    LOGGER(DEBUGGING, __func__, " -->");
    if (publicKey.getType() != privateKey.getType()) {
        LOGGER(ERROR, __func__, " <-- key types don't match");
        return KEY_TYPE_MISMATCH;
    }

    int32_t curveType = publicKey.getType();
    if (curveType == EcCurveTypes::Curve25519) {
        if (length < Ec255PrivateKey::KEY_LENGTH) {
            LOGGER(ERROR, __func__, " <-- key buffer too small");
            return BUFFER_TOO_SMALL;
        }

        // curve25519_donna always returns 0, thus ignore the return code
        curve25519_donna(agreement, privateKey.privateData(), publicKey.getPublicKeyPointer());
        return Ec255PublicKey::KEY_LENGTH;
    }
    LOGGER(DEBUGGING, __func__, " <-- unsupported curve type");
    return NO_SUCH_CURVE;
}

// bool EcCurve::verifySignature(const DhPublicKey& signingKey, const uint8_t* message, size_t msgLength, const uint8_t* signature, size_t signLength )
// {
//     int32_t curveType = signingKey.getType();
// 
//     if (curveType == EcCurveTypes::Curve25519) {
//         if (signLength < Ec255PrivateKey::SIGN_LENGTH)
//             return false;
// 
//         int32_t result = curve25519_verify(signature, signingKey.getPublicKeyPointer(),  message, msgLength);
//         return result == 0;
//     }
//     return false;
// }
// 
// int32_t EcCurve::calculateSignature(const DhPrivateKey& signingKey, const uint8_t* message, size_t msgLength, uint8_t* signature, size_t signLength)
// {
//     int32_t curveType = signingKey.getType();
//     if (curveType == EcCurveTypes::Curve25519) {
//         if (signLength < Ec255PrivateKey::SIGN_LENGTH)
//             return BUFFER_TOO_SMALL;
// 
//         unsigned char random[Ec255PrivateKey::SIGN_LENGTH];
//         ZrtpRandom::getRandomData(random, Ec255PrivateKey::SIGN_LENGTH);
// 
//         int32_t result = curve25519_sign(signature, signingKey.privateData(), message, msgLength, random);
//     }
//     return NO_SUCH_CURVE;
// }
// 
PublicKeyUnique EcCurve::decodePoint(const uint8_t* bytes)
{
    LOGGER(DEBUGGING, __func__, " -->");
    int32_t type = *bytes & 0xFF;

    if (type == EcCurveTypes::Curve25519) {
        LOGGER(DEBUGGING, __func__, " <--");
        return PublicKeyUnique(new Ec255PublicKey(bytes+1));
    }
    LOGGER(WARNING, __func__, " <-- unsupported curve type");
    return PublicKeyUnique(nullptr);
}
