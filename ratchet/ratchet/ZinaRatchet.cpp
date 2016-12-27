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
#include "ZinaRatchet.h"

#include "../ZinaPreKeyConnector.h"
#include "../crypto/EcCurve.h"
#include "../crypto/AesCbc.h"
#include "../crypto/HKDF.h"
#include "../../logging/ZinaLogging.h"
#include "../../interfaceApp/MessageEnvelope.pb.h"
#include "../../util/Utilities.h"

#include <zrtp/crypto/hmac256.h>
#include <zrtp/crypto/sha256.h>
#include <common/osSpecifics.h>


#ifdef UNITTESTS
// Used in testing and debugging to do in-depth checks
static char hexBuffer[2000] = {0};
static void hexdump(const char* title, const unsigned char *s, size_t l) {
    size_t n=0;

    if (s == NULL) return;

    memset(hexBuffer, 0, 2000);
    int len = sprintf(hexBuffer, "%s",title);
    for( ; n < l ; ++n)
    {
        if((n%16) == 0)
            len += sprintf(hexBuffer+len, "\n%04x",static_cast<int>(n));
        len += sprintf(hexBuffer+len, " %02x",s[n]);
    }
    sprintf(hexBuffer+len, "\n");
}
static void hexdump(const char* title, const string& in)
{
    hexdump(title, (uint8_t*)in.data(), in.size());
}
#endif

using namespace zina;

typedef struct parsedMessage_ {
    uint32_t msgType;
    uint32_t curveType;
    uint32_t version;
    uint32_t flags;

    int32_t maxVersion;
    uint32_t Np;
    uint32_t PNp;
    uint32_t contextId;

    const uint8_t*  ratchet;

    const uint8_t*  mac;

    int32_t   localPreKeyId;
    const uint8_t*  remotePreKey;
    const uint8_t*  remoteIdKey;

    size_t  encryptedMsgLen;
    const uint8_t*  encryptedMsg;
} ParsedMessage;

static int32_t deriveRkCk(ZinaConversation& conv, string* newRK, string* newCK)
{
    LOGGER(INFO, __func__, " -->");
    uint8_t agreement[MAX_KEY_BYTES];

    // Compute a DH agreement from the current Ratchet keys: use receiver's (remote party's) public key and sender's
    // (local party's) private key
    int32_t agreementLength = EcCurve::calculateAgreement(*conv.getDHRr(), conv.getDHRs()->getPrivateKey(),
                                                          agreement, (size_t)MAX_KEY_BYTES);
    if (agreementLength < 0) {
        LOGGER(ERROR, __func__, " <-- agreement computation failed");
        return agreementLength;
    }
    size_t length = static_cast<size_t>(agreementLength);

    // We need to derive key data for two keys: the new RK and the sender's CK (CKs), thus use
    // a larger buffer
    uint8_t derivedSecretBytes[MAX_KEY_BYTES*2];

    // Use HDKF with 3 input parameters: ikm, salt, info
    HKDF::deriveSecrets(agreement, length,                                  // agreement as input key material to HASH KDF
                       (uint8_t*)conv.getRK().data(), SYMMETRIC_KEY_LENGTH, // conv.getRK().size(),   // the current root key as salt
                       (uint8_t*)SILENT_RATCHET_DERIVE.data(), 
                        SILENT_RATCHET_DERIVE.size(),                       // fixed string "SilentCircleRKCKDerive" as info
                       derivedSecretBytes, SYMMETRIC_KEY_LENGTH*2);

    memset_volatile((void*)agreement, 0, MAX_KEY_BYTES);

    newRK->assign((const char*)derivedSecretBytes, length);
    newCK->assign((const char*)derivedSecretBytes+length, length);
//     hexdump("deriveRkCk old RK", conv.getRK());  LOGGER(VERBOSE, hexBuffer);
//     hexdump("deriveRkCk RK", *newRK);  LOGGER(VERBOSE, hexBuffer);
//     hexdump("deriveRkCk CK", *newCK);  LOGGER(VERBOSE, hexBuffer);
    memset_volatile((void*)derivedSecretBytes, 0, MAX_KEY_BYTES*2);
    LOGGER(INFO, __func__, " <--");
    return OK;
}

static void deriveMk(const string& chainKey, string* MK, string* iv, string* macKey)
{
    LOGGER(INFO, __func__, " -->");
    uint8_t ckMac[SHA256_DIGEST_LENGTH];
    uint32_t ckMacLen;

    // MK = HMAC-HASH(CKs, "0")
    // Hash CKs with "0"
    hmac_sha256((uint8_t*)chainKey.data(), SYMMETRIC_KEY_LENGTH, (uint8_t*)"0", 1, ckMac, &ckMacLen);

    // We need a key, an IV, and a MAC key
    uint8_t keyMaterialBytes[SYMMETRIC_KEY_LENGTH + AES_BLOCK_SIZE + SYMMETRIC_KEY_LENGTH];

    // Use HKDF with 2 input parameters: ikm, info. The salt is SAH256 hash length 0 bytes
    HKDF::deriveSecrets((uint8_t*)ckMac, ckMacLen,                      // input key material: hashed CKs
                        (uint8_t*)SILENT_MSG_DERIVE.data(), 
                        SILENT_MSG_DERIVE.size(),                       // fixed string "SilentCircleMessageKeyDerive" as info
                        keyMaterialBytes, SYMMETRIC_KEY_LENGTH+AES_BLOCK_SIZE+SYMMETRIC_KEY_LENGTH);

    memset_volatile((void*)ckMac, 0, SHA256_DIGEST_LENGTH);

    MK->assign((const char*)keyMaterialBytes, SYMMETRIC_KEY_LENGTH);
    iv->assign((const char*)keyMaterialBytes+SYMMETRIC_KEY_LENGTH, AES_BLOCK_SIZE);
    macKey->assign((const char*)keyMaterialBytes+SYMMETRIC_KEY_LENGTH+AES_BLOCK_SIZE, SYMMETRIC_KEY_LENGTH);

    memset_volatile((void*)keyMaterialBytes, 0, SYMMETRIC_KEY_LENGTH+AES_BLOCK_SIZE+SYMMETRIC_KEY_LENGTH);
    LOGGER(INFO, __func__, " <--");
}


#define FIXED_TYPE1_OVERHEAD  (4 + 4 + 4 + 4 + 8)
#define ADD_TYPE2_OVERHEAD    (4)

static void createWireMessageV1(ZinaConversation &conv, string &message, string &mac, string* wire)
{
    LOGGER(INFO, __func__, " -->");
    // Determine the wire message type:
    // 1: Normal message with new Ratchet key
    // 2: Message with new Ratchet Key and pre-key information

    uint8_t msgType;

    // A0 is set only if we use pre-keys and this is 'Alice' and generated a pre-key info, thus
    // wire message type 2 only if we use pre-key initialization
    msgType = static_cast<uint8_t>((conv.getA0() == NULL) ? 1 : 2);

    // The code below currently uses the curve 25519 only. This curve requires 32 byte key data.
    // To support other curves we need to adapt that code
    // The general wire message format:
    /*
       msgType:   1 byte
       curveType: 1 byte
       version:   1 byte
       flags:     1 byte
       Ns:        4 byte integer (network order)
       PNs:       4 byte integer (network order)
       DHRs:      32 byte ratchet key
       mac:       8 byte mac, truncated hmac256 of encrypted message
       if msgType == 2
           4 byte integer (network order) remote pre-key id
           32 byte Alice's identity key
           32 byte Alice's local, generated pre-key

       encrytedMsgLen: 4 byte integer (network order), encrypted message length
       encryptedMsg: variable number of bytes
     */
    size_t keyLength = EcCurveTypes::Curve25519KeyLength;  // fixed for curve 25519
    size_t msgLength = FIXED_TYPE1_OVERHEAD + keyLength;   // at least a msg type, Ns, PNs, and message length

    if (msgType == 2) {
        msgLength += ADD_TYPE2_OVERHEAD + keyLength + keyLength;    // add remote pre-key id, local generated pre-key, identity key
    }
    msgLength += message.size();

    uint8_t* wireMessage = new uint8_t[msgLength];
    uint8_t* wmPb = wireMessage;
    int32_t* wmPi = (int32_t*)wireMessage;
    int32_t byteIndex = 0;
    int32_t intIndex = 0;

    wmPb[byteIndex++] = msgType;
    wmPb[byteIndex++] = EcCurveTypes::Curve25519;
    wmPb[byteIndex++] = 1;
    wmPb[byteIndex++] = 0;
    intIndex++;

    wmPi[intIndex++] = zrtpHtonl(static_cast<uint32_t>(conv.getNs())); byteIndex += sizeof(uint32_t);
    wmPi[intIndex++] = zrtpHtonl(static_cast<uint32_t>(conv.getPNs())); byteIndex += sizeof(uint32_t);

    const DhPublicKey& rKey = conv.getDHRs()->getPublicKey();
    memcpy(&wmPb[byteIndex], rKey.getPublicKeyPointer(), rKey.getSize());   // sizes are currently Curve25519KeyLength
    intIndex += rKey.getSize()/sizeof(int32_t); byteIndex += rKey.getSize();

    memcpy(&wmPb[byteIndex], mac.data(), 8);
    intIndex += 8/sizeof(int32_t); byteIndex += 8;

    if (msgType == 2) {
        // set remote pre-key id, always a positive value, thus cast
        wmPi[intIndex++] = zrtpHtonl(static_cast<uint32_t>(conv.getPreKeyId())); byteIndex += sizeof(uint32_t);

        const DhPublicKey& idKey = conv.getDHIs()->getPublicKey();   // copy the public identity key
        memcpy(&wmPb[byteIndex], idKey.getPublicKeyPointer(), idKey.getSize());
        intIndex += idKey.getSize()/sizeof(int32_t); byteIndex += idKey.getSize();

        const DhPublicKey& a0Key = conv.getA0()->getPublicKey();   // copy the local generated pre-key
        memcpy(&wmPb[byteIndex], a0Key.getPublicKeyPointer(), a0Key.getSize());
        intIndex += a0Key.getSize()/sizeof(int32_t); byteIndex += a0Key.getSize();
    }
    wmPi[intIndex] = zrtpHtonl(static_cast<uint32_t>(message.size())); byteIndex += sizeof(uint32_t);
    memcpy(&wmPb[byteIndex], message.data(), message.size());

    wire->assign((const char*)wireMessage, msgLength);
//    hexdump("create wire", *wire); Log("%s", hexBuffer);
    LOGGER(INFO, __func__, " <--");
}

// Set up the message (enevlope) for protocol version 2 or better. If the version number changes this
// function is the right place to handle protocol differences. Also see parseWireMsgVx.
static void createWireMessageVx(ZinaConversation &conv, MessageEnvelope& envelope, string& encryptedData,
                                string& computedMac, int32_t useVersion)
{
    envelope.set_message(encryptedData);

    // Determine the wire message type:
    // 1: Normal message with new Ratchet key
    // 2: Message with new Ratchet Key and pre-key information, set-up context

    // A0 is set only if we use pre-keys and this is 'Alice' and generated a pre-key info, thus
    // wire message type 2 only if we use pre-key initialization
    int32_t msgType = static_cast<uint8_t>((conv.getA0() == NULL) ? 1 : 2);

    RatchetData* ratchet = envelope.mutable_ratchet();
    ratchet->set_ratchetmsgtype(msgType);
    ratchet->set_curvetype(EcCurveTypes::Curve25519);

    ratchet->set_useversion(useVersion);

    ratchet->set_np(conv.getNs());
    ratchet->set_pnp(conv.getPNs());

    const DhPublicKey& rKey = conv.getDHRs()->getPublicKey();
    ratchet->set_ratchet(conv.getDHRs()->getPublicKey().getPublicKey());

    ratchet->set_mac(computedMac.data(), 8);

    if (msgType == 2) {
        ratchet->set_localprekeyid(conv.getPreKeyId());
        ratchet->set_remoteidkey(conv.getDHIs()->getPublicKey().getPublicKey());
        ratchet->set_remoteprekey(conv.getA0()->getPublicKey().getPublicKey());
    }
}

// Parse a wire message and setup a structure with data from and pointers into wire message.
//
static int32_t parseWireMsgV1(const string &wire, ParsedMessage *msgStruct)
{
//    hexdump("parse wire", wire); LOGGER(VERBOSE, hexBuffer);

    LOGGER(INFO, __func__, " -->");
    const uint8_t* data = (const uint8_t*)wire.data();
    const uint32_t* dPi = (uint32_t*)data;
    size_t byteIndex = 0;
    size_t intIndex = 0;

    size_t keyDataLength = EcCurveTypes::Curve25519KeyLength;
    size_t expectedLength = FIXED_TYPE1_OVERHEAD + keyDataLength;

    msgStruct->msgType = data[byteIndex++] & 0xffU;
    msgStruct->curveType = data[byteIndex++] & 0xffU;
    msgStruct->version = data[byteIndex++] & 0xffU;
    msgStruct->flags = data[byteIndex++] & 0xffU;
    intIndex++;

    msgStruct->Np = zrtpNtohl(dPi[intIndex++]); byteIndex += sizeof(uint32_t);
    msgStruct->PNp = zrtpNtohl(dPi[intIndex++]); byteIndex += sizeof(uint32_t);

    msgStruct->ratchet = &data[byteIndex];
    intIndex += keyDataLength/sizeof(uint32_t); byteIndex += keyDataLength;

    msgStruct->mac = &data[byteIndex];
    intIndex += 8/sizeof(uint32_t); byteIndex += 8;

    if (msgStruct->msgType == 2) {
        expectedLength += ADD_TYPE2_OVERHEAD + keyDataLength + keyDataLength;
        msgStruct->localPreKeyId = zrtpNtohl(dPi[intIndex++]); byteIndex += sizeof(uint32_t);

        msgStruct->remoteIdKey = &data[byteIndex];
        intIndex += keyDataLength/sizeof(uint32_t); byteIndex += keyDataLength;

        msgStruct->remotePreKey = &data[byteIndex];
        intIndex += keyDataLength/sizeof(uint32_t); byteIndex += keyDataLength;
    }
    else {
        msgStruct->localPreKeyId = 0;
        msgStruct->remoteIdKey = nullptr;
        msgStruct->remotePreKey = nullptr;
    }
    msgStruct->encryptedMsgLen = zrtpNtohl(dPi[intIndex]); byteIndex += sizeof(int32_t);
    msgStruct->encryptedMsg = &data[byteIndex];
    if ((byteIndex + msgStruct->encryptedMsgLen) > wire.size()) {
        msgStruct->encryptedMsg = NULL;
        msgStruct->encryptedMsgLen = 0;
    }
    expectedLength += msgStruct->encryptedMsgLen;
    if (expectedLength != wire.size()) {
        LOGGER(INFO, __func__, " <-- data length mismatch.");
        return RECV_DATA_LENGTH;
    }
    LOGGER(INFO, __func__, " <--");
    return SUCCESS;
}

static int32_t parseWireMsgVx(const MessageEnvelope &envelope, ParsedMessage *msgStruct)
{

    // ATTENTION: this holds a pointer into the MessageEnvelope data structure. This OK because the
    // the lifetime of ParsedMessage is shorter than the envelope.

    // Proto buffer uses C++ string to store byte data (binary data), thus in this case it's
    // just a container. Thus we need a reinterpret_cast here to set the uint8_t* without
    // compiler warning.
    msgStruct->encryptedMsg = reinterpret_cast<const uint8_t*>(envelope.message().data());
    msgStruct->encryptedMsgLen = envelope.message().size();

    const RatchetData& ratchet = envelope.ratchet();

    msgStruct->msgType = static_cast<uint32_t>(ratchet.ratchetmsgtype());
    msgStruct->curveType = static_cast<uint32_t>(ratchet.curvetype());
    msgStruct->version = static_cast<uint32_t>(ratchet.useversion());
    msgStruct->flags = ratchet.has_flags() ? ratchet.flags() : 0;

    msgStruct->Np = static_cast<uint32_t>(ratchet.np());
    msgStruct->PNp = static_cast<uint32_t>(ratchet.pnp());

    // ATTENTION: same note as for encryptedMsg above
    msgStruct->ratchet = reinterpret_cast<const uint8_t*>(ratchet.ratchet().data());
    msgStruct->mac = reinterpret_cast<const uint8_t*>(ratchet.mac().data());

    // This is a context setup message, get additional data
    if (msgStruct->msgType == 2) {
        msgStruct->localPreKeyId = ratchet.localprekeyid();
        msgStruct->remoteIdKey = reinterpret_cast<const uint8_t*>(ratchet.remoteidkey().data());
        msgStruct->remotePreKey = reinterpret_cast<const uint8_t*>(ratchet.remoteprekey().data());
    }
    else {
        msgStruct->localPreKeyId = 0;
        msgStruct->remoteIdKey = nullptr;
        msgStruct->remotePreKey = nullptr;
    }
    return SUCCESS;
}

static int32_t decryptAndCheck(const string& MK, const string& iv, const string& encrypted, const string& supplements, const string& macKey,
                               const string& mac, string* decrypted, string* supplementsPlain, bool expectFail=false)
{

    LOGGER(INFO, __func__, " -->");

    uint32_t macLen;
    uint8_t computedMac[SHA256_DIGEST_LENGTH];

    hmac_sha256((uint8_t*)macKey.data(), (uint32_t)macKey.size(), (uint8_t*)encrypted.data(),
                static_cast<int32_t>(encrypted.size()), computedMac, &macLen);

    // During the trySkippedMessageKeys we expect MAC failure because we try the staged
    // message keys in a "brute-force" mode ;-)
    int32_t result = memcmp(computedMac, mac.data(), SHORT_MAC_LENGTH);
    if (result != 0) {
        if (expectFail) {
            LOGGER(INFO, __func__, " <-- MAC check failed - expected.");
        }
        else {
            LOGGER(ERROR, __func__, " <-- MAC check failed.");
        }
        return MAC_CHECK_FAILED;
    }

    // If MAC is OK then treat every other failure as ERROR
    int32_t ret = aesCbcDecrypt(MK, iv, encrypted, decrypted);
    if (ret != SUCCESS) {
        LOGGER(ERROR, __func__, " <-- Decrypt failed.");
        return ret;
    }
    if (!checkAndRemovePadding(decrypted)) {
        LOGGER(ERROR, __func__, " <-- Padding check failed.");
        return MSG_PADDING_FAILED;
    }

    if (supplements.size() > 0 && supplementsPlain) {
        ret = aesCbcDecrypt(MK, iv, supplements, supplementsPlain);
        if (ret != SUCCESS) {
            LOGGER(ERROR, __func__, " <-- Decrypt failed (supplements).");
            return ret;
        }
        if (!checkAndRemovePadding(supplementsPlain)) {
            LOGGER(ERROR, __func__, " <-- Padding check failed (supplements).");
            return SUP_PADDING_FAILED;
        }
    }
    LOGGER(INFO, __func__, " <--");
    return SUCCESS;
}

static int32_t trySkippedMessageKeys(ZinaConversation* conv, const string& encrypted, const string& supplements, const string& mac,
                                     string* plaintext, string* supplementsPlain)
{
    LOGGER(INFO, __func__, " -->");

    int32_t retVal = MAC_CHECK_FAILED;              // Initialize to an expected error return, see decryptAndCheck(...)
    shared_ptr<list<string> > mks = conv->loadStagedMks();
    if (!mks || mks->empty()) {
        if (conv->getErrorCode() != SUCCESS) {
            LOGGER(INFO, __func__, " <-- Error reading MK: ", conv->getErrorCode(), ", DB code: ", conv->getSqlErrorCode());
        }
        conv->clearStagedMks(mks);
        LOGGER(INFO, __func__, " <-- No staged keys.");
        return NO_STAGED_KEYS;
    }

    // During the loop we expect that decryptAndCheck fails
    for (auto it = mks->begin(); it != mks->end(); ++it) {
        string& MKiv = *it;
        if (MKiv.size() < SYMMETRIC_KEY_LENGTH + AES_BLOCK_SIZE + SHORT_MAC_LENGTH)
            continue;
        string MK = MKiv.substr(0, SYMMETRIC_KEY_LENGTH);
        string iv = MKiv.substr(SYMMETRIC_KEY_LENGTH, AES_BLOCK_SIZE);
        string macKey = MKiv.substr(SYMMETRIC_KEY_LENGTH + AES_BLOCK_SIZE);
        if ((retVal = decryptAndCheck(MK, iv, encrypted, supplements, macKey, mac, plaintext, supplementsPlain, true)) == SUCCESS) {
            conv->deleteStagedMk(MKiv);
        }
        // First really clear the memory, the set size to 0.
        Utilities::wipeString(MK); MK.clear();
        Utilities::wipeString(iv); iv.clear();
        Utilities::wipeString(macKey); macKey.clear();
        if (retVal == SUCCESS) {
            break;
        }
    }
    // Clear staged keys really clears memory of list data and resets the list to null
    conv->clearStagedMks(mks);
    LOGGER(INFO, __func__, " <-- ", (retVal != SUCCESS) ? "no matching MK found." : "matching MK found");
    return retVal;
}

static int32_t stageSkippedMessageKeys(ZinaConversation* conv, int32_t Nr, int32_t Np, const string& CKr, string* CKp,
                                    pair<string, string>* MKp, string* macKey)
{
    LOGGER(INFO, __func__, " -->");

    string MK;
    string iv;
    string mKey;

    uint8_t ckMac[SHA256_DIGEST_LENGTH];
    uint32_t ckMacLen;
    *CKp = CKr;

    shared_ptr<list<string> > mks = conv->getEmptyStagedMks();
    if (conv->getErrorCode() != SUCCESS)
        return conv->getErrorCode();

    for (int32_t i = Nr; i < Np; i++) {
        deriveMk(*CKp, &MK, &iv, &mKey);

        // Use append here to work around GCC's Copy-On-Write behaviour (COW) for strings.
        string mkivmac;
        mkivmac.append(MK).append(iv).append(mKey);
        mks->push_back(mkivmac);

        // Hash CK with "1"
        hmac_sha256((uint8_t*)CKp->data(), SYMMETRIC_KEY_LENGTH, (uint8_t*)"1", 1, ckMac, &ckMacLen);
        CKp->assign((const char*)ckMac, ckMacLen);

        Utilities::wipeString(MK); MK.clear();
        Utilities::wipeString(iv); iv.clear();
        Utilities::wipeString(mKey); mKey.clear();

        mkivmac.append(" ");    // COW workaround
        Utilities::wipeString(mkivmac); mkivmac.clear();
    }
    deriveMk(*CKp, &MK, &iv, &mKey);

    // Use assign here to work around GCC's Copy-On-Write behaviour for strings.
    MKp->first.assign(MK.c_str(), MK.size());
    MKp->second.assign(iv.c_str(), iv.size());
    macKey->assign(mKey.c_str(), mKey.size());

    // Hash CK with "1"
    hmac_sha256((uint8_t*)CKp->data(), SYMMETRIC_KEY_LENGTH, (uint8_t*)"1", 1, ckMac, &ckMacLen);
    CKp->assign((const char*)ckMac, ckMacLen);

    Utilities::wipeString(MK); MK.clear();
    Utilities::wipeString(iv); iv.clear();
    Utilities::wipeString(mKey); mKey.clear();
    memset_volatile((void*)ckMac, 0, SHA256_DIGEST_LENGTH);

    LOGGER(INFO, __func__, " <--");
    return SUCCESS;
}

static void computeIdHash(const string& id, string* idHash)
{
    LOGGER(INFO, __func__, " -->");
    uint8_t hash[SHA256_DIGEST_LENGTH];

    sha256((uint8_t*)id.data(), static_cast<uint>(id.size()), hash);
    idHash->assign((const char*)hash, SHA256_DIGEST_LENGTH);
    LOGGER(INFO, __func__, " <--");
}

static int32_t compareHashes(pair<string, string>* idHashes, string& recvIdHash, string& senderIdHash)
{
    LOGGER(INFO, __func__, " -->");
    string id = idHashes->first;
    if (id.compare(0, id.size(), recvIdHash, 0, id.size()) != 0) {
        LOGGER(ERROR, __func__, " <-- Receive ID wrong");
        return RECEIVE_ID_WRONG;
    }

    id = idHashes->second;
    if (id.compare(0, id.size(), senderIdHash, 0, id.size()) != 0) {
        LOGGER(ERROR, __func__, " <-- Sender ID wrong");
        return SENDER_ID_WRONG;
    }
    LOGGER(INFO, __func__, " <--");
    return SUCCESS;
}
/*
if (plaintext = try_skipped_header_and_message_keys()):
  return plaintext

if HKr != <none> and Dec(HKr, header):
  Np = read()
  CKp, MK = stage_skipped_header_and_message_keys(HKr, Nr, Np, CKr)
  if not Dec(MK, ciphertext):
    raise undecryptable
else:
  if ratchet_flag or not Dec(NHKr, header):
    raise undecryptable()
  Np = read()
  PNp = read()
  DHRp = read()
  stage_skipped_header_and_message_keys(HKr, Nr, PNp, CKr)
  HKp = NHKr
  RKp, NHKp, CKp = KDF( HMAC-HASH(RK, DH(DHRp, DHRs)) )
  CKp, MK = stage_skipped_header_and_message_keys(HKp, 0, Np, CKp)
  if not Dec(MK, ciphertext):
    raise undecryptable()
  RK = RKp
  HKr = HKp
  NHKr = NHKp
  DHRr = DHRp
  erase(DHRs)
  ratchet_flag = True
commit_skipped_header_and_message_keys()
Nr = Np + 1
CKr = CKp
return read()
 */
static shared_ptr<const string>decryptInternal(ZinaConversation* conv, ParsedMessage& msgStruct, const string& supplements,
    string* supplementsPlain, pair<string, string>* idHashes)
{
    // The partner shows that it supports a proto version > 1, save this in the context data
    // When creating a message to this partner we use this info to create a message with the
    // best available version number we support.
    conv->setVersionNumber(msgStruct.maxVersion);

    int32_t result;

    // This is a message with embedded pre-key and identity key. Need to setup the
    // Axolotl conversation first. According to the optimized pre-key handling this
    // client takes the Axolotl 'Bob' role.
    if (msgStruct.msgType == 2) {
        const Ec255PublicKey* aliceId = new Ec255PublicKey(msgStruct.remoteIdKey);
        const Ec255PublicKey* alicePreKey = new Ec255PublicKey(msgStruct.remotePreKey);
        conv->setContextId(msgStruct.contextId);
        result = ZinaPreKeyConnector::setupConversationBob(conv, msgStruct.localPreKeyId, aliceId, alicePreKey);
        if (result != SUCCESS)
            return shared_ptr<string>();
    }

    // Check if conversation is really setup - identity key must be available in any case
    if (conv->getDHIr() == NULL) {
        conv->setErrorCode(SESSION_NOT_INITED);
        return shared_ptr<string>();
    }
    conv->setVersionNumber(msgStruct.maxVersion);

    if (conv->getContextId() != 0 && msgStruct.contextId != 0 && conv->getContextId() != msgStruct.contextId) {
        LOGGER(ERROR, __func__, " <-- Context ID mismatch, message ignored: ", conv->getContextId(), ", ", msgStruct.contextId);
        conv->setErrorCode(OLD_MESSAGE);
        return shared_ptr<string>();
    }

    if (idHashes != nullptr) {
        string recvIdHash;
        auto localConv = ZinaConversation::loadLocalConversation(conv->getLocalUser());
        if (localConv->isValid()) {
            const string idPub = localConv->getDHIs()->getPublicKey().getPublicKey();
            computeIdHash(idPub, &recvIdHash);
        }
        string senderIdHash;
        const string idPub = conv->getDHIr()->getPublicKey();
        computeIdHash(idPub, &senderIdHash);
        result = compareHashes(idHashes, recvIdHash, senderIdHash);
        if (result != SUCCESS) {
            conv->setErrorCode(result);
            return shared_ptr<string>();
        }
    }

    string encrypted((const char*)msgStruct.encryptedMsg, msgStruct.encryptedMsgLen);
    shared_ptr<string> decrypted = make_shared<string>();

    string mac((const char*)msgStruct.mac, 8);
    if (trySkippedMessageKeys(conv, encrypted, supplements, mac, decrypted.get(), supplementsPlain) == SUCCESS) {
        return decrypted;
    }

    const DhPublicKey* DHRp = new Ec255PublicKey(msgStruct.ratchet);
    bool newRatchet = conv->getDHRr() == NULL || !(*DHRp == *(conv->getDHRr()));

    string RKp;
    string CKp;
    string macKey;
    pair <string, string> MK;
//    LOGGER(ERROR, "++++ Decrypt message from: ", conv->getPartner().getName(), " Nr: ", conv->getNr(), " Np: ", msgStruct.Np, " PNp: ", msgStruct.PNp, " newR: ", newRatchet);

    if (!newRatchet) {
        delete(DHRp);
        int32_t status = stageSkippedMessageKeys(conv, conv->getNr(), msgStruct.Np, conv->getCKr(), &CKp, &MK, &macKey);
        if (status != SUCCESS) {
            LOGGER(ERROR, __func__, " <-- Old ratchet, staging MK failed, error codes: ", conv->getErrorCode(), ", ", conv->getSqlErrorCode());
            return shared_ptr<string>();
        }
        status = decryptAndCheck(MK.first, MK.second, encrypted, supplements,  macKey, mac, decrypted.get(), supplementsPlain);
        if (status != SUCCESS) {
            LOGGER(ERROR, __func__, " <-- Old ratchet, decrypt failed, staged MK not stored.");
            conv->setErrorCode(status);
            return shared_ptr<string>();
        }
    }
    else {
        // Stage the skipped message for the current (old) ratchet, CKp, MK and macKey are not
        // used at this point, PNp has the max number of message sent on the old ratchet
        int32_t status = stageSkippedMessageKeys(conv, conv->getNr(), msgStruct.PNp, conv->getCKr(), &CKp, &MK, &macKey);
        if (status != SUCCESS) {
            LOGGER(ERROR, __func__, " <-- New ratchet, staging MK for old ratchet failed, error codes: ", conv->getErrorCode(), ", ", conv->getSqlErrorCode());
            return shared_ptr<string>();
        }

        // Save old DHRr, may need to restore in case of failure
        const DhPublicKey* saveDHRr = conv->getDHRr();

        // set up the new ratchet DHRr and derive the new RK and CKr from it
        conv->setDHRr(DHRp);

        // RKp, CKp = KDF( HMAC-HASH(RK, DH(DHRp, DHRs)) )
        // With the new ratchet key derive the purported RK and CKr
        status = deriveRkCk(*conv, &RKp, &CKp);
        if (status < 0) {
            conv->setDHRr(saveDHRr);
            delete DHRp;
            conv->setErrorCode(status);
            LOGGER(ERROR, __func__, " <-- New ratchet, failed to derive RKp/CKp, staged MK not stored.");
            return shared_ptr<string>();
        }

        // With a new ratchet the message nr starts at zero, however we may have missed
        // the first message with the new ratchet key, thus stage up to purported number and
        // compute the chain key starting with the purported chain key computed above
        status = stageSkippedMessageKeys(conv, 0, msgStruct.Np, CKp, &CKp, &MK, &macKey);
        if (status != SUCCESS) {
            conv->setDHRr(saveDHRr);
            delete DHRp;
            LOGGER(ERROR, __func__, " <-- New ratchet, staging MK failed, error codes: ", conv->getErrorCode(), ", ", conv->getSqlErrorCode());
            return shared_ptr<string>();
        }

        status = decryptAndCheck(MK.first, MK.second, encrypted, supplements, macKey, mac, decrypted.get(), supplementsPlain);
        if (status != SUCCESS) {
            conv->setDHRr(saveDHRr);
            delete DHRp;
            conv->setErrorCode(status);
            LOGGER(ERROR, __func__, " <-- New ratchet, failed to decrypt, new staged MK not stored.");
            return shared_ptr<string>();
        }
        conv->setRK(RKp);
        RKp.append(" ");            // Use append here to work around GCC's COW for strings.
        Utilities::wipeString(RKp); RKp.clear();
        delete saveDHRr;
        conv->setRatchetFlag(true);
    }
    conv->setCKr(CKp);
    CKp.append(" ");                // Use append here to work around GCC's COW for strings.
    Utilities::wipeString(CKp); CKp.clear();

    conv->setNr(msgStruct.Np + 1);  // Receiver: expected next message number

    // Here we can delete A0 in case it was set, if this was Alice then Bob replied and
    // A0 is not needed anymore.
    delete(conv->getA0());
    conv->setA0(NULL);

    Utilities::wipeString(macKey); macKey.clear();

    LOGGER(INFO, __func__, " <--");
    return decrypted;
}

shared_ptr<const string> ZinaRatchet::decrypt(ZinaConversation* conv, MessageEnvelope& envelope, string* supplementsPlain)
{
    LOGGER(INFO, __func__, " -->");

    int32_t useVersion = 1;
    int32_t result = 0;
    ParsedMessage msgStruct;

    memset(&msgStruct, 0, sizeof(msgStruct));
    msgStruct.maxVersion = 1;               // Initialize with minimum version

    // A client supporting protocol version 2 or better created this message. In this case get some
    // data now and not during parsing.
    if (envelope.has_ratchet()) {
        useVersion = envelope.ratchet().useversion();
        if (useVersion > SUPPORTED_VERSION) {
            conv->setErrorCode(VERSION_NOT_SUPPORTED);
            return shared_ptr<string>();
        }
        msgStruct.maxVersion = envelope.ratchet().maxversion();
        msgStruct.contextId = envelope.ratchet().contextid();
    }
    if (useVersion > 1) {
        result = parseWireMsgVx(envelope, &msgStruct);
    }
    else {
        const string& wire = envelope.message();
        result = parseWireMsgV1(wire, &msgStruct);
    }
    if (msgStruct.encryptedMsg == NULL) {
        conv->setErrorCode(CORRUPT_DATA);
        return shared_ptr<string>();
    }
    if (result < 0) {
        conv->setErrorCode(result);
        return shared_ptr<string>();
    }

    pair<string, string> idHashes;
    bool hasIdHashes = false;
    if (envelope.has_recvidhash() && envelope.has_senderidhash()) {
        hasIdHashes = true;
        const string& recvIdHash = envelope.recvidhash();
        const string& senderIdHash = envelope.senderidhash();
        idHashes.first = recvIdHash;
        idHashes.second = senderIdHash;
    }

    const string& supplements = envelope.has_supplement() ? envelope.supplement() : Empty;

    return decryptInternal(conv, msgStruct, supplements, supplementsPlain, hasIdHashes ? &idHashes : nullptr);
}

/*
 Sending messages
-----------------
Local variables:
  MK  : message key

if ratchet_flag:
  DHRs = generateECDH()
  HKs = NHKs
  RK, NHKs, CKs = KDF( HMAC-HASH(RK, DH(DHRs, DHRr)) )
  PNs = Ns
  Ns = 0
  ratchet_flag = False

MK = HMAC-HASH(CKs, "0")
msg = Enc(HKs, Ns || PNs || DHRs) || Enc(MK, plaintext)
Ns = Ns + 1
CKs = HMAC-HASH(CKs, "1")
return msg

 *
 * This implementation does not use header keys.
 */
int32_t
ZinaRatchet::encrypt(ZinaConversation& conv, const string& message, MessageEnvelope& envelope, const string &supplements)
{
    LOGGER(INFO, __func__, " -->");
    if (conv.getRK().empty()) {
        conv.setErrorCode(SESSION_NOT_INITED);
        return SESSION_NOT_INITED;
    }

    // Encrypt the user's message and the supplementary data if necessary
    pair<string, string> idHashes;

    string wireMessage;
    string supplementsEncrypted;;

    string senderIdHash;

    auto localConv = ZinaConversation::loadLocalConversation(conv.getLocalUser());
    if (localConv->isValid()) {
        const string idPub = localConv->getDHIs()->getPublicKey().getPublicKey();
        computeIdHash(idPub, &senderIdHash);
    }

    string recvIdHash;
    const string idPub = conv.getDHIr()->getPublicKey();
    computeIdHash(idPub, &recvIdHash);
    idHashes.first = recvIdHash;
    idHashes.second = senderIdHash;

    bool ratchetSave = conv.getRatchetFlag();

    if (ratchetSave) {
        const DhKeyPair *oldDHRs = conv.getDHRs();
        const DhKeyPair *newDHRs = EcCurve::generateKeyPair(EcCurveTypes::Curve25519);
        conv.setDHRs(newDHRs);
        delete oldDHRs;
        string newRK;
        string newCK;
        deriveRkCk(conv, &newRK, &newCK);
        conv.setRK(newRK);
        conv.setCKs(newCK);
        conv.setPNs(conv.getNs());
        conv.setNs(0);
        conv.setRatchetFlag(false);
    }
    string MK;
    string iv;
    string macKey;
    deriveMk(conv.getCKs(), &MK, &iv, &macKey);

    string encryptedData;

    int32_t ret = aesCbcEncrypt(MK, iv, message, &encryptedData);
    if (ret != SUCCESS) {
        conv.setErrorCode(ret);
        LOGGER(ERROR, __func__, " <-- Encryption failed.");
        return ret;
    }
    if (!supplements.empty()) {
        ret = aesCbcEncrypt(MK, iv, supplements, &supplementsEncrypted);
        if (ret != SUCCESS) {
            conv.setErrorCode(ret);
            LOGGER(ERROR, __func__, " <-- Encryption failed (supplements).");
            return ret;
        }
    }
    Utilities::wipeString(MK); MK.clear();
    Utilities::wipeString(iv); iv.clear();

    uint8_t mac[SHA256_DIGEST_LENGTH];
    uint32_t macLen;
    hmac_sha256((uint8_t *) macKey.data(), (uint32_t) macKey.size(), (uint8_t *) encryptedData.data(),
                static_cast<int32_t>(encryptedData.size()), mac, &macLen);

    Utilities::wipeString(macKey); macKey.clear();
    string computedMac((const char *) mac, SHA256_DIGEST_LENGTH);

    // if partner supports a better version than we: use our supported version, else the version of out partner
    // which may be lower than our version number. A new partner's conversation currently has a initial version
    // number 0 which is treated as version 1. This is for backward compatibility with older clients.
    // If we dismiss version 1 sometimes in the future a new conversation will have another initial version number.

    // The first message after the initial set-up message from our partner contains the partner's supported
    // version. The receiver functions (in decrypt, parseWireMessageVx) handle this and store the partner's
    // version in its conversation (ratchet context).
    // The receiver uses the 'useVersion' to call the correct parser. Old clients will always use V1 because their
    // version values are either 0 or 1
    int32_t useVersion = conv.getVersionNumber() >= SUPPORTED_VERSION ? SUPPORTED_VERSION : conv.getVersionNumber();
    useVersion = useVersion == 0 ? 1 : useVersion;      // version 0 is the same as version 1

    RatchetData* ratchet = envelope.mutable_ratchet();
    if (useVersion <= 1) {
        createWireMessageV1(conv, encryptedData, computedMac, &wireMessage);
        envelope.set_message(wireMessage);

        ratchet->set_useversion(1);
    }
    else {
        createWireMessageVx(conv, envelope, encryptedData, computedMac, useVersion);
    }

    // Common fields for all protocol versions in envelope.
    ratchet->set_maxversion(SUPPORTED_VERSION);
    ratchet->set_contextid(conv.getContextId());

    if (!supplementsEncrypted.empty())
        envelope.set_supplement(supplementsEncrypted);
    envelope.set_recvidhash(idHashes.first.data(), 4);
    envelope.set_senderidhash(idHashes.second.data(), 4);

//    LOGGER(ERROR, "++++ Encrypt message to:   ", conv.getPartner().getName(), " Nr: ", conv.getNr(), " Np: ", conv.getNs(), " PNp: ", conv.getPNs(), " newR: ", ratchetSave);
    // After creating the wire message update the conversation (ratchet context) data
    conv.setNs(conv.getNs() + 1);

    // Hash CKs with "1"
    hmac_sha256((uint8_t*)conv.getCKs().data(), SYMMETRIC_KEY_LENGTH, (uint8_t*)"1", 1, mac, &macLen);
    string newCKs((const char*)mac, macLen);
    conv.setCKs(newCKs);

    LOGGER(INFO, __func__, " <--");
    return SUCCESS;
}