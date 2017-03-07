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

#include "AppInterfaceImpl.h"

#include "../keymanagment/PreKeys.h"
#include "../util/b64helper.h"
#include "../provisioning/Provisioning.h"
#include "../provisioning/ScProvisioning.h"
#include "../dataRetention/ScDataRetention.h"
#include "JsonStrings.h"
#include "../util/Utilities.h"

#include <cryptcommon/ZrtpRandom.h>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"

using namespace zina;

static string requestGroupSyncCommand(const string& deviceId)
{
    JsonUnique sharedRoot(cJSON_CreateObject());
    cJSON* root = sharedRoot.get();
    cJSON_AddStringToObject(root, GROUP_COMMAND, REQUEST_GROUPS_SYNC);
    cJSON_AddStringToObject(root, MSG_DEVICE_ID, deviceId.c_str());

    CharUnique out(cJSON_PrintUnformatted(root));
    string command(out.get());

    return command;
}

AppInterfaceImpl::AppInterfaceImpl(const string& ownUser, const string& authorization, const string& scClientDevId,
                                   RECV_FUNC receiveCallback, STATE_FUNC stateReportCallback, NOTIFY_FUNC notifyCallback,
                                   GROUP_MSG_RECV_FUNC groupMsgCallback, GROUP_CMD_RECV_FUNC groupCmdCallback,  GROUP_STATE_FUNC groupStateCallback):
        AppInterface(receiveCallback, stateReportCallback, notifyCallback, groupMsgCallback, groupCmdCallback, groupStateCallback),
        tempBuffer_(NULL), tempBufferSize_(0), ownUser_(ownUser), authorization_(authorization), scClientDevId_(scClientDevId),
        errorCode_(0), transport_(NULL), flags_(0), siblingDevicesScanned_(false), drLrmm_(false), drLrmp_(false), drLrap_(false),
        drBldr_(false), drBlmr_(false), drBrdr_(false), drBrmr_(false)
{
    store_ = SQLiteStoreConv::getStore();
    ScDataRetention::setAuthorization(authorization);
}

AppInterfaceImpl::~AppInterfaceImpl()
{
    LOGGER(DEBUGGING, __func__, " -->");
    tempBufferSize_ = 0; delete tempBuffer_; tempBuffer_ = NULL;
    delete transport_; transport_ = NULL;
    LOGGER(DEBUGGING, __func__, " <--");
}

string AppInterfaceImpl::createSupplementString(const string& attachmentDesc, const string& messageAttrib)
{
    LOGGER(DEBUGGING, __func__, " -->");
    string supplement;
    if (!attachmentDesc.empty() || !messageAttrib.empty()) {
        cJSON* msgSupplement = cJSON_CreateObject();

        if (!attachmentDesc.empty()) {
            LOGGER(VERBOSE, "Adding an attachment descriptor supplement");
            cJSON_AddStringToObject(msgSupplement, "a", attachmentDesc.c_str());
        }

        if (!messageAttrib.empty()) {
            LOGGER(VERBOSE, "Adding an message attribute supplement");
            cJSON_AddStringToObject(msgSupplement, "m", messageAttrib.c_str());
        }
        char *out = cJSON_PrintUnformatted(msgSupplement);

        supplement = out;
        cJSON_Delete(msgSupplement); free(out);
    }
    LOGGER(DEBUGGING, __func__, " <--");
    return supplement;
}


string* AppInterfaceImpl::getKnownUsers()
{
    int32_t sqlCode;

    LOGGER(DEBUGGING, __func__, " -->");
    if (!store_->isReady()) {
        LOGGER(ERROR, __func__, " Axolotl conversation DB not ready.");
        return NULL;
    }

    shared_ptr<list<string> > names = store_->getKnownConversations(ownUser_, &sqlCode);

    if (SQL_FAIL(sqlCode) || !names) {
        LOGGER(INFO, __func__, " No known Axolotl conversations.");
        return NULL;
    }
    size_t size = names->size();
    if (size == 0)
        return NULL;

    cJSON *root,*nameArray;
    root=cJSON_CreateObject();
    cJSON_AddItemToObject(root, "version", cJSON_CreateNumber(1));
    cJSON_AddItemToObject(root, "users", nameArray = cJSON_CreateArray());

    for (int32_t i = 0; i < size; i++) {
        const string& name = names->front();
        cJSON_AddItemToArray(nameArray, cJSON_CreateString(name.c_str()));
        names->pop_front();
    }
    char *out = cJSON_PrintUnformatted(root);
    string* retVal = new string(out);
    cJSON_Delete(root); free(out);

    LOGGER(DEBUGGING, __func__, " <--");
    return retVal;
}

/*
 * JSON data for a registration request:
{
    "version" :        <int32_t>,        # Version of JSON registration, 1 for the first implementation
    "identity_key" :    <string>,         # public part encoded base64 data 
    "prekeys" : [{
        "id" :     <int32_t>,         # The key id of the signed pre key
        "key" :       <string>,          # public part encoded base64 data
    },
    ....
    {
        "id" :     <int32_t>,         # The key id of the signed pre key
        "key" :       <string>,          # public part encoded base64 data
    }]
}
 */
int32_t AppInterfaceImpl::registerZinaDevice(string* result)
{
    cJSON *root;
    char b64Buffer[MAX_KEY_BYTES_ENCODED*2];   // Twice the max. size on binary data - b64 is times 1.5

    LOGGER(DEBUGGING, __func__, " -->");

    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
//    cJSON_AddStringToObject(root, "scClientDevId", scClientDevId_.c_str());

    shared_ptr<ZinaConversation> ownConv = ZinaConversation::loadLocalConversation(ownUser_, *store_);
    if (!ownConv->isValid()) {
        cJSON_Delete(root);
        LOGGER(ERROR, __func__, " No own conversation in database.");
        return NO_OWN_ID;
    }
    const DhKeyPair* myIdPair = ownConv->getDHIs();
    if (myIdPair == NULL) {
        cJSON_Delete(root);
        LOGGER(ERROR, __func__, " Own conversation not correctly initialized.");
        return NO_OWN_ID;
    }

    string data = myIdPair->getPublicKey().serialize();

    b64Encode((const uint8_t*)data.data(), data.size(), b64Buffer, MAX_KEY_BYTES_ENCODED*2);
    cJSON_AddStringToObject(root, "identity_key", b64Buffer);

    cJSON* jsonPkrArray;
    cJSON_AddItemToObject(root, "prekeys", jsonPkrArray = cJSON_CreateArray());

    list<pair<int32_t, const DhKeyPair* > >* preList = PreKeys::generatePreKeys(store_);

    for (; !preList->empty(); preList->pop_front()) {
        const pair< int32_t, const DhKeyPair* >& pkPair = preList->front();

        cJSON* pkrObject;
        cJSON_AddItemToArray(jsonPkrArray, pkrObject = cJSON_CreateObject());
        cJSON_AddNumberToObject(pkrObject, "id", pkPair.first);

        // Get pre-key's public key data, serialized
        const DhKeyPair* ecPair = pkPair.second;
        const string keyData = ecPair->getPublicKey().serialize();

        b64Encode((const uint8_t*) keyData.data(), keyData.size(), b64Buffer, MAX_KEY_BYTES_ENCODED * 2);
        cJSON_AddStringToObject(pkrObject, "key", b64Buffer);
        delete ecPair;
    }
    delete preList;

    char *out = cJSON_PrintUnformatted(root);
    string registerRequest(out);
    cJSON_Delete(root); free(out);

    int32_t code = Provisioning::registerZinaDevice(registerRequest, authorization_, scClientDevId_, result);

    if (code == 200) {
        requestGroupsSync();
    }
    LOGGER(DEBUGGING, __func__, " <-- ", code);
    return code;
}

int32_t AppInterfaceImpl::removeZinaDevice(string& devId, string* result)
{
    LOGGER(DEBUGGING, __func__, " <-->");
    return ScProvisioning::removeZinaDevice(devId, authorization_, result);
}

int32_t AppInterfaceImpl::newPreKeys(int32_t number)
{
    LOGGER(DEBUGGING, __func__, " -->");
    string result;
    return ScProvisioning::newPreKeys(store_, scClientDevId_, authorization_, number, &result);
}

int32_t AppInterfaceImpl::getNumPreKeys() const
{
    LOGGER(DEBUGGING, __func__, " <-->");
    return Provisioning::getNumPreKeys(scClientDevId_, authorization_);
}

// Get known Axolotl device from provisioning server, check if we have a new one
// and if yes send a "ping" message to the new devices to create an Axolotl conversation
// for the new devices. The real implementation is in the command handling function below.

void AppInterfaceImpl::rescanUserDevices(string& userName)
{
    LOGGER(DEBUGGING, __func__, " -->");

    auto msgInfo = new CmdQueueInfo;
    msgInfo->command = ReScanUserDevices;
    msgInfo->queueInfo_recipient = userName;
    addMsgInfoToRunQueue(unique_ptr<CmdQueueInfo>(msgInfo));

    LOGGER(DEBUGGING, __func__, " <--");
    return;
}


void AppInterfaceImpl::setHttpHelper(HTTP_FUNC httpHelper)
{
    ScProvisioning::setHttpHelper(httpHelper);
    ScDataRetention::setHttpHelper(httpHelper);
}

void AppInterfaceImpl::setS3Helper(S3_FUNC s3Helper)
{
    ScDataRetention::setS3Helper(s3Helper);
}

void AppInterfaceImpl::reKeyAllDevices(string &userName) {
    list<StringUnique> devices;

    if (!store_->isReady()) {
        LOGGER(ERROR, __func__, " Axolotl conversation DB not ready.");
        return;
    }
    store_->getLongDeviceIds(userName, ownUser_, devices);
    for (auto &recipientDeviceId : devices) {
        reSyncConversation(userName, *recipientDeviceId);
    }
}

void AppInterfaceImpl::reSyncConversation(const string &userName, const string& deviceId) {
    LOGGER(DEBUGGING, __func__, " -->");

    if (!store_->isReady()) {
        LOGGER(ERROR, __func__, " Axolotl conversation DB not ready.");
        return;
    }
    // Don't re-sync this device
    bool toSibling = userName == ownUser_;
    if (toSibling && deviceId == scClientDevId_) {
        return;
    }
    auto msgInfo = new CmdQueueInfo;
    msgInfo->command = ReSyncDeviceConversation;
    msgInfo->queueInfo_recipient = userName;
    msgInfo->queueInfo_deviceId = deviceId;
    msgInfo->boolData1 = toSibling;
    addMsgInfoToRunQueue(unique_ptr<CmdQueueInfo>(msgInfo));

    LOGGER(DEBUGGING, __func__, " <--");
    return;
}

// ***** Private functions
// *******************************

int32_t AppInterfaceImpl::parseMsgDescriptor(const string& messageDescriptor, string* recipient, string* msgId, string* message, bool receivedMsg)
{
    LOGGER(DEBUGGING, __func__, " -->");
    cJSON* cjTemp;
    char* jsString;

    // wrap the cJSON root into a shared pointer with custom cJSON deleter, this
    // will always free the cJSON root when we leave the function :-) .
    shared_ptr<cJSON> sharedRoot(cJSON_Parse(messageDescriptor.c_str()), cJSON_deleter);
    cJSON* root = sharedRoot.get();

    if (root == NULL) {
        errorInfo_ = "root";
        return GENERIC_ERROR;
    }
    const char* recipientSender = receivedMsg ? MSG_SENDER : MSG_RECIPIENT;
    cjTemp = cJSON_GetObjectItem(root, recipientSender);
    jsString = (cjTemp != NULL) ? cjTemp->valuestring : NULL;
    if (jsString == NULL) {
        errorInfo_ = recipientSender;
        return JS_FIELD_MISSING;
    }
    recipient->assign(jsString);

    // Get the message id
    cjTemp = cJSON_GetObjectItem(root, MSG_ID);
    jsString = (cjTemp != NULL) ? cjTemp->valuestring : NULL;
    if (jsString == NULL) {
        errorInfo_ = MSG_ID;
        return JS_FIELD_MISSING;
    }
    msgId->assign(jsString);

    // Get the message
    cjTemp = cJSON_GetObjectItem(root, MSG_MESSAGE);
    jsString = (cjTemp != NULL) ? cjTemp->valuestring : NULL;
    if (jsString == NULL) {
        errorInfo_ = MSG_MESSAGE;
        return JS_FIELD_MISSING;
    }
    message->assign(jsString);

    LOGGER(DEBUGGING, __func__, " <--");
    return OK;
}

string AppInterfaceImpl::getOwnIdentityKey()
{
    LOGGER(DEBUGGING, __func__, " -->");

    char b64Buffer[MAX_KEY_BYTES_ENCODED*2];   // Twice the max. size on binary data - b64 is times 1.5
    shared_ptr<ZinaConversation> axoConv = ZinaConversation::loadLocalConversation(ownUser_, *store_);
    if (!axoConv->isValid()) {
        LOGGER(ERROR, "No own conversation, ignore.")
        LOGGER(INFO, __func__, " <-- No own conversation.");
        errorInfo_ = "Failed to read own conversation from database";
        errorCode_ = axoConv->getErrorCode();
        return Empty;
    }

    const DhKeyPair* keyPair = axoConv->getDHIs();
    const DhPublicKey& pubKey = keyPair->getPublicKey();

    b64Encode(pubKey.getPublicKeyPointer(), pubKey.getSize(), b64Buffer, MAX_KEY_BYTES_ENCODED*2);

    string idKey((const char*)b64Buffer);
    idKey.append(":");
    if (!axoConv->getDeviceName().empty()) {
        idKey.append(axoConv->getDeviceName());
    }
    idKey.append(":").append(scClientDevId_).append(":0");
    LOGGER(DEBUGGING, __func__, " <--");
    return idKey;
}

shared_ptr<list<string> > AppInterfaceImpl::getIdentityKeys(string& user)
{
    LOGGER(DEBUGGING, __func__, " -->");

    char b64Buffer[MAX_KEY_BYTES_ENCODED*2];   // Twice the max. size on binary data - b64 is times 1.5
    shared_ptr<list<string> > idKeys = make_shared<list<string> >();

    list<StringUnique> devices;
    store_->getLongDeviceIds(user, ownUser_, devices);

    for (auto &recipientDeviceId : devices) {
        auto axoConv = ZinaConversation::loadConversation(ownUser_, user, *recipientDeviceId, *store_);
        errorCode_ = axoConv->getErrorCode();
        if (errorCode_ != SUCCESS || !axoConv->isValid()) { // A database problem when loading the conversation
            errorInfo_ = "Failed to read remote conversation from database";
            idKeys->clear();                // return an empty list, all gathered info may be invalid
            return idKeys;
        }
        const DhPublicKey* idKey = axoConv->getDHIr();
        if (idKey == NULL) {
            continue;
        }

        b64Encode(idKey->getPublicKeyPointer(), idKey->getSize(), b64Buffer, MAX_KEY_BYTES_ENCODED*2);

        string id((const char*)b64Buffer);
        id.append(":");
        if (!axoConv->getDeviceName().empty()) {
            id.append(axoConv->getDeviceName());
        }
        id.append(":").append(*recipientDeviceId);
        snprintf(b64Buffer, 5, ":%d", axoConv->getZrtpVerifyState());
        b64Buffer[4] = '\0';          // make sure it's terminated
        id.append(b64Buffer);

        idKeys->push_back(id);
    }
    LOGGER(DEBUGGING, __func__, " <--");
    return idKeys;
}


void AppInterfaceImpl::reSyncConversationCommand(const CmdQueueInfo &command) {
    LOGGER(DEBUGGING, __func__, " -->");

    if (!store_->isReady()) {
        LOGGER(ERROR, __func__, " ZINA conversation DB not ready.");
        return;
    }
    // clear data and store the nearly empty conversation
    shared_ptr<ZinaConversation> conv = ZinaConversation::loadConversation(ownUser_, command.queueInfo_recipient, command.queueInfo_deviceId, *store_);
    if (!conv->isValid()) {
        return;
    }
    conv->reset();
    conv->storeConversation(*store_);
    if (conv->getErrorCode() != SUCCESS) {
        return;
    }

    // Check if server still knows this device.
    // If no device at all for his user -> remove all conversations (ratchet contexts) of this user.
    list<pair<string, string> > devices;
    int32_t errorCode = Provisioning::getZinaDeviceIds(command.queueInfo_recipient, authorization_, devices);

    if (errorCode != SUCCESS || devices.empty()) {
        store_->deleteConversationsName(command.queueInfo_recipient, ownUser_);
        return;
    }
    bool deviceFound = false;
    string deviceName;
    for (const auto &device : devices) {
        if (command.queueInfo_deviceId == device.first) {
            deviceName = device.second;
            deviceFound = true;
            break;
        }
    }

    // The server does not know this device anymore. In this case remove the conversation (ratchet context), done.
    if (!deviceFound) {
        store_->deleteConversation(command.queueInfo_recipient, command.queueInfo_deviceId, ownUser_);
        return;
    }

    queueMessageToSingleUserDevice(command.queueInfo_recipient, generateMsgIdTime(), command.queueInfo_deviceId,
                                   deviceName, ping, Empty, MSG_NORMAL, 0, true);

    LOGGER(DEBUGGING, __func__, " <--");
    return;
}

void AppInterfaceImpl::setIdKeyVerified(const string &userName, const string& deviceId, bool flag) {
    LOGGER(DEBUGGING, __func__, " -->");

    if (!store_->isReady()) {
        LOGGER(ERROR, __func__, " Axolotl conversation DB not ready.");
        return;
    }
    // Don't do this for own devices
    bool toSibling = userName == ownUser_;
    if (toSibling && deviceId == scClientDevId_)
        return;

    auto msgInfo = new CmdQueueInfo;
    msgInfo->command = SetIdKeyChangeFlag;
    msgInfo->queueInfo_recipient = userName;
    msgInfo->queueInfo_deviceId = deviceId;
    msgInfo->boolData1 = flag;
    addMsgInfoToRunQueue(unique_ptr<CmdQueueInfo>(msgInfo));

    LOGGER(DEBUGGING, __func__, " <--");
    return;
}

int32_t AppInterfaceImpl::setDataRetentionFlags(const string& jsonFlags)
{
    LOGGER(DEBUGGING, __func__, " --> ", jsonFlags);
    if (jsonFlags.empty()) {
        return DATA_MISSING;
    }

    shared_ptr<cJSON> sharedRoot(cJSON_Parse(jsonFlags.c_str()), cJSON_deleter);
    cJSON* root = sharedRoot.get();
    if (root == nullptr) {
        return CORRUPT_DATA;
    }
    drLrmm_ = Utilities::getJsonBool(root, LRMM, false);
    drLrmp_ = Utilities::getJsonBool(root, LRMP, false);
    drLrap_ = Utilities::getJsonBool(root, LRAP, false);
    drBldr_ = Utilities::getJsonBool(root, BLDR, false);
    drBlmr_ = Utilities::getJsonBool(root, BLMR, false);
    drBrdr_ = Utilities::getJsonBool(root, BRDR, false);
    drBrmr_ = Utilities::getJsonBool(root, BRMR, false);

    LOGGER(DEBUGGING, __func__, " <--");
    return SUCCESS;
}

void AppInterfaceImpl::checkRemoteIdKeyCommand(const CmdQueueInfo &command)
{
    /*
     * Command data usage:
    command.command = CkeckRemoteIdKey;
    command.stringData1 = remoteName;
    command.stringData2 = deviceId;
    command.stringData3 = pubKey;
    command.int32Data = verifyState;
     */
    auto remote = ZinaConversation::loadConversation(getOwnUser(), command.stringData1, command.stringData2, *store_);

    if (!remote->isValid()) {
        LOGGER(ERROR, "<-- No conversation, user: '", command.stringData1, "', device: ", command.stringData2);
        return;
    }
    const DhPublicKey* remoteId = remote->getDHIr();
    const string remoteIdKey = remoteId->getPublicKey();

    if (command.stringData3.compare(remoteIdKey) != 0) {
        LOGGER(ERROR, "<-- Messaging keys do not match, user: '", command.stringData1, "', device: ", command.stringData2);
        return;
    }
    // if verifyState is 1 then both users verified their SAS and thus set the Axolotl conversation
    // to fully verified, otherwise at least the identity keys are equal and we proved that via
    // a ZRTP session.
    int32_t verify = (command.int32Data == 1) ? 2 : 1;
    remote->setZrtpVerifyState(verify);
    remote->setIdentityKeyChanged(false);
    remote->storeConversation(*store_);
}

void AppInterfaceImpl::setIdKeyVerifiedCommand(const CmdQueueInfo &command)
{
    /*
     * Command data usage:
    command.command = SetIdKeyChangeFlag;
    command.queueInfo_recipient = remoteName;
    command.queueInfo_deviceId = deviceId;
    command.boolData1 = flag;
     */
    auto remote = ZinaConversation::loadConversation(getOwnUser(), command.queueInfo_recipient, command.queueInfo_deviceId, *store_);

    if (!remote->isValid()) {
        LOGGER(ERROR, "<-- No conversation, user: '", command.queueInfo_recipient, "', device: ", command.queueInfo_deviceId);
        return;
    }
    remote->setIdentityKeyChanged(command.boolData1);
    remote->storeConversation(*store_);
}

void AppInterfaceImpl::rescanUserDevicesCommand(const CmdQueueInfo &command)
{
    LOGGER(DEBUGGING, __func__, " -->");

    const string &userName = command.queueInfo_recipient;

    list<pair<string, string> > devices;
    int32_t errorCode = Provisioning::getZinaDeviceIds(userName, authorization_, devices);

    if (errorCode != SUCCESS || devices.empty()) {
        return;
    }

    // Get known devices from DB, compare with devices from provisioning server
    // and remove old devices in DB, i.e. devices not longer known to provisioning server
    //
    list<StringUnique> devicesDb;

    store_->getLongDeviceIds(userName, ownUser_, devicesDb);

    for (const auto &devIdDb : devicesDb) {
        bool found = false;

        for (const auto &device : devices) {
            if (*devIdDb == device.first) {
                found = true;
                break;
            }
        }
        if (!found) {
            store_->deleteConversation(userName, *devIdDb, ownUser_);
            LOGGER(INFO, __func__, "Remove device from database: ", *devIdDb);
        }
    }

    // Prepare and send this to the new learned device:
    // - an Empty message
    // - a message command attribute with a ping command
    // For each Ping message the code generates a new UUID

    // Prepare the messages for all known new devices of this user

    int64_t transportMsgId;
    ZrtpRandom::getRandomData(reinterpret_cast<uint8_t*>(&transportMsgId), 8);
    uint64_t counter = 0;

    // The transport id is structured: bits 0..3 are status/type bits, bits 4..7 is a counter, bits 8..63 random data
    transportMsgId &= ~0xff;

    for (const auto &device : devices) {
        const string& deviceId = device.first;
        const string& deviceName = device.second;

        // Don't re-scan own device, just check if name changed
        bool toSibling = userName == ownUser_;
        if (toSibling && scClientDevId_ == deviceId) {
            shared_ptr<ZinaConversation> conv = ZinaConversation::loadLocalConversation(ownUser_, *store_);
            if (conv->isValid()) {
                const string &convDevName = conv->getDeviceName();
                if (deviceName.compare(convDevName) != 0) {
                    conv->setDeviceName(deviceName);
                    conv->storeConversation(*store_);
                }
            }
            continue;
        }

        // If we already have a conversation for this device skip further processing
        // after storing a user defined device name. The user may change a device's name
        // using the Web interface of the provisioning server
        if (store_->hasConversation(userName, deviceId, ownUser_)) {
            shared_ptr<ZinaConversation> conv = ZinaConversation::loadConversation(ownUser_, userName, deviceId, *store_);
            if (conv->isValid()) {
                const string &convDevName = conv->getDeviceName();
                if (deviceName.compare(convDevName) != 0) {
                    conv->setDeviceName(deviceName);
                    conv->storeConversation(*store_);
                }
            }
            continue;
        }

        LOGGER(INFO, __func__, "Send Ping to new found device: ", deviceId);
        queueMessageToSingleUserDevice(userName, generateMsgIdTime(), deviceId, deviceName, ping, Empty, MSG_NORMAL, counter, true);
        counter++;

        LOGGER(DEBUGGING, "Queued message to ping a new device.");
    }
    LOGGER(DEBUGGING, __func__, " <--");
    return;
}

void AppInterfaceImpl::queueMessageToSingleUserDevice(const string &userId, const string &msgId, const string &deviceId,
                                                      const string &deviceName, const string &attributes,
                                                      const string &msg, int32_t msgType, int64_t counter, bool newDevice)
{
    LOGGER(DEBUGGING, __func__, " --> ");

    int64_t transportMsgId;
    ZrtpRandom::getRandomData(reinterpret_cast<uint8_t*>(&transportMsgId), 8);

    // The transport id is structured: bits 0..3 are status/type bits, bits 4..7 is a counter, bits 8..63 random data
    transportMsgId &= ~0xff;

    auto msgInfo = new CmdQueueInfo;
    msgInfo->command = SendMessage;
    msgInfo->queueInfo_recipient = userId;
    msgInfo->queueInfo_deviceName = deviceName;
    msgInfo->queueInfo_deviceId = deviceId;                     // to this user device
    msgInfo->queueInfo_msgId = msgId;
    msgInfo->queueInfo_message = createMessageDescriptor(userId, msgId, msg);
    msgInfo->queueInfo_attachment = Empty;                      // No attachments
    msgInfo->queueInfo_attributes = attributes;                 // message attributes
    msgInfo->queueInfo_transportMsgId = transportMsgId | (counter << 4) | static_cast<uint64_t>(msgType);
    msgInfo->queueInfo_toSibling = userId == getOwnUser();
    msgInfo->queueInfo_newUserDevice = newDevice;
    addMsgInfoToRunQueue(unique_ptr<CmdQueueInfo>(msgInfo));

    LOGGER(DEBUGGING, __func__, " <-- ");
}

int32_t AppInterfaceImpl::requestGroupsSync()
{
    list<pair<string, string> > devices;
    int32_t errorCode = Provisioning::getZinaDeviceIds(getOwnUser(), authorization_, devices);

    // Provisioning server knows no or only one device, must be this device.
    if (errorCode != SUCCESS || devices.size() <= 1) {
        return SUCCESS;
    }

    const string requestCmd = requestGroupSyncCommand(getOwnDeviceId());
    for (auto &device : devices) {
        if (device.first == getOwnDeviceId()) {
            continue;
        }
        queueMessageToSingleUserDevice(getOwnUser(), generateMsgIdTime(), device.first, Empty, requestCmd, Empty, GROUP_MSG_CMD, 0, false);
    }
    return SUCCESS;
}
#pragma clang diagnostic pop
