
#include "Transaction.h"
#include "../BRCrypto.h"
#include "../BRBIP32Sequence.h"
#include "../log.h"
#include "../BRAddress.h"

#define SIGNATURE_SCRIPT_LENGTH     65

Transaction::Transaction()
    : mTxVersion(0)
    , mType(TransferAsset)
    , mPayloadVersion(0)
    , mPayload(nullptr)
    , mLockTime(TX_LOCKTIME)
    , mFee(0)
    , mTxHash(UINT256_ZERO)
{
}

Transaction::~Transaction()
{
    for(UTXOInput* input : mInputs) {
        delete input;
    }
    mInputs.clear();

    for(TxOutput* output : mOutputs) {
        delete output;
    }
    mOutputs.clear();

    for (Attribute* attr : mAttributes) {
        delete attr;
    }
    mAttributes.clear();

    for(Program* program : mPrograms) {
        delete program;
    }
    mPrograms.clear();
}

UInt256 Transaction::GetHash()
{
    UInt256 emptyHash = UINT256_ZERO;
    if (UInt256Eq(&mTxHash, &emptyHash)) {
        ByteStream ostream;
        SerializeUnsigned(ostream);
        CMBlock buff = ostream.getBuffer();
        BRSHA256_2(&mTxHash, buff, buff.GetSize());
    }
    return mTxHash;
}

void Transaction::Serialize(ByteStream &ostream)
{
    SerializeUnsigned(ostream);
    CMBlock data = ostream.getBuffer();

    ostream.writeVarUint(mPrograms.size());
    for (size_t i = 0; i < mPrograms.size(); i++) {
        mPrograms[i]->Serialize(ostream);
    }
}

CMBlock Transaction::SignData(const CMBlock& privateKey)
{
    CMBlock publicKey;
    publicKey.Resize(33);
    getPubKeyFromPrivKey(publicKey, (UInt256 *)(uint8_t *)privateKey);

    printf("sign public key: ");
    Utils::printBinary(publicKey, publicKey.GetSize());

    CMBlock shaData = GetSHAData();

    CMBlock signedData;
    signedData.Resize(65);
    ECDSA65Sign_sha256(privateKey, privateKey.GetSize(),
            (const UInt256 *) &shaData[0], signedData, signedData.GetSize());

    printf("signed data: ");
    Utils::printBinary(signedData, signedData.GetSize());
    return signedData;
}

CMBlock Transaction::GetSHAData()
{
    ByteStream ostream;
    SerializeUnsigned(ostream);
    CMBlock data = ostream.getBuffer();

    printf("unsigned data: ");
    Utils::printBinary(data, data.GetSize());

    CMBlock shaData(sizeof(UInt256));
    BRSHA256(shaData, data, data.GetSize());
    return shaData;
}

void Transaction::Sign(const CMBlock & privateKey)
{
    CMBlock signedData = SignData(privateKey);

    CMBlock publicKey;
    publicKey.Resize(33);
    getPubKeyFromPrivKey(publicKey, (UInt256 *)(uint8_t *)privateKey);

    CMBlock code = Utils::getCode(publicKey);

    Program* program = new Program(code, signedData);
    if (program) {
        mPrograms.push_back(program);
    }
}

void Transaction::MultiSign(const CMBlock& privateKey, const CMBlock& redeemScript)
{
    if (mPrograms.empty()) {
        Program* program = new Program();
        if (!program) return;

        program->mCode = redeemScript;
        mPrograms.push_back(program);
    }

    if (mPrograms.size() != 1) {
        printf("Multi-sign program should be unique.\n");
        return;
    }

    ByteStream stream;
    if (mPrograms[0]->mParameter.GetSize() > 0) {
        stream.putBytes(mPrograms[0]->mParameter, mPrograms[0]->mParameter.GetSize());
    }

    CMBlock signedData = SignData(privateKey);
    stream.putBytes(signedData, signedData.GetSize());
    mPrograms[0]->mParameter = stream.getBuffer();
}

std::vector<std::string> Transaction::GetSignedSigner()
{
    if (mPrograms.size() > 1) {
        WALLET_C_LOG("not multi sign transaction!\n");
        return std::vector<std::string>();
    }

    if (mPrograms.size() == 0) {
        WALLET_C_LOG("transaction not signed!\n");
        return std::vector<std::string>();
    }

    Program* program = mPrograms[0];
    const CMBlock& code = program->GetCode();
    if (code[code.GetSize() - 1] != ELA_MULTISIG) {
        WALLET_C_LOG("not multi sign transaction!\n");
        return std::vector<std::string>();
    }

    std::vector<std::string> signers;
    for (int i = 1; i < code.GetSize() - 2;) {
        uint8_t size = code[i];
        signers.push_back(Utils::encodeHex(&code[i + 1], size));
        i += size + 1;
    }

    CMBlock shaData = GetSHAData();
    UInt256 md;
    memcpy(md.u8, shaData, sizeof(UInt256));

    const CMBlock &parameter = program->GetParameter();
    std::vector<std::string> signedSigners;

    for (int i = 0; i < parameter.GetSize(); i += SIGNATURE_SCRIPT_LENGTH) {
        CMBlock signature(SIGNATURE_SCRIPT_LENGTH);
        memcpy(signature, &parameter[i], SIGNATURE_SCRIPT_LENGTH);

        for (std::string signer : signers) {

            if (Verify(signer, md, signature))
                signedSigners.push_back(signer);
        }
    }

    return signedSigners;
}

bool Transaction::Verify(const std::string& publicKey, const UInt256& messageDigest, const CMBlock& signature)
{
    CMBlock pubKey = Utils::decodeHex(publicKey);
    return ECDSA65Verify_sha256((uint8_t *) (void *) pubKey, pubKey.GetSize(), &messageDigest, signature,
                                signature.GetSize()) != 0;
}

std::vector<CMBlock> Transaction::GetPrivateKeys()
{
    std::vector<CMBlock> privateKeys;
    for (UTXOInput* input : mInputs) {
        if (input->mPrivateKey.GetSize() == 0) {
            continue;
        }

        if (privateKeys.empty()) {
            privateKeys.push_back(input->mPrivateKey);
        }
        else {
            bool found = false;
            for (int i = 0; i < privateKeys.size(); i++) {
                if (memcmp(privateKeys[i], input->mPrivateKey, input->mPrivateKey.GetSize()) == 0) {
                    found = true;
                    break;
                }
            }

            if (found) continue;

            privateKeys.push_back(input->mPrivateKey);
        }
    }

    return privateKeys;
}

void Transaction::SerializeUnsigned(ByteStream &ostream) const
{
    if (mTxVersion != 0) {
        ostream.writeBytes(&mTxVersion, 1);
    }

    ostream.writeBytes(&mType, 1);

    ostream.writeBytes(&mPayloadVersion, 1);

    //Payload
    if (mCrossChainAssets.size() > 0){
        ostream.putVarUint(mCrossChainAssets.size());
        for (CrossChainAsset* crossChainAsset : mCrossChainAssets) {
            crossChainAsset->Serialize(ostream);
        }
    }

    ostream.writeVarUint(mAttributes.size());
    for (size_t i = 0; i < mAttributes.size(); i++) {
        mAttributes[i]->Serialize(ostream);
    }

    ostream.writeVarUint(mInputs.size());
    for (size_t i = 0; i < mInputs.size(); i++) {
        mInputs[i]->Serialize(ostream);
    }

    ostream.writeVarUint(mOutputs.size());
    for (size_t i = 0; i < mOutputs.size(); i++) {
        mOutputs[i]->Serialize(ostream, mTxVersion);
    }

    ostream.writeUint32(mLockTime);
}

void Transaction::FromJson(const nlohmann::json &jsonData, const std::string& assertId)
{
    std::vector<nlohmann::json> jUtxoInputs = jsonData["UTXOInputs"];

    for (nlohmann::json utxoInput : jUtxoInputs) {
        UTXOInput* input = new UTXOInput();
        if (input) {
            input->FromJson(utxoInput);
            mInputs.push_back(input);
        }
    }

    std::vector<nlohmann::json> jTxOuputs = jsonData["Outputs"];
    for(nlohmann::json txOutput : jTxOuputs) {
        TxOutput* output = new TxOutput(assertId);
        if (output) {
            output->FromJson(txOutput);
            mOutputs.push_back(output);
        }
    }

    for (TxOutput* output : mOutputs) {
        if (output->GetVersion() == 9) {
            mTxVersion = 9;
            break;
        }
    }

    auto jPrograms = jsonData.find("Programs");
    if (jPrograms != jsonData.end()) {
        std::vector<nlohmann::json> programs = jsonData["Programs"];
        for (nlohmann::json program : programs) {
            Program* pProgram = new Program();
            if (pProgram) {
                pProgram->FromJson(program);
                mPrograms.push_back(pProgram);
            }
        }
    }

    auto jAttrs = jsonData.find("Attributes");
    if (jAttrs != jsonData.end()) {
        std::vector<nlohmann::json> attrs = jsonData["Attributes"];
        for (nlohmann::json attribute : attrs) {
            Attribute* pAttr = new Attribute(Attribute::Usage::Nonce, "");
            if (pAttr) {
                pAttr->FromJson(attribute);
                mAttributes.push_back(pAttr);
            }
        }
    }
    else {
        std::string memo;
        auto jMemo = jsonData.find("Memo");
        if (jMemo != jsonData.end()) {
            memo = jsonData["Memo"].get<std::string>();
        }

        Attribute* pAttr = new Attribute(memo.empty() ? Attribute::Usage::Nonce : Attribute::Usage::Memo, memo);
        if (pAttr) {
            mAttributes.push_back(pAttr);
        }

        std::string postmark;
        auto jPostmark = jsonData.find("Postmark");
        if (jPostmark != jsonData.end()) {
            nlohmann::json postmarkObj = jsonData["Postmark"];
            postmark = postmarkObj.dump();
            postmark.insert(0, "{\"Postmark\":");
            postmark.append("}");
            Attribute* pAttr = new Attribute(Attribute::Usage::Description, postmark);
            if (pAttr) {
                mAttributes.push_back(pAttr);
            }
        }
    }

    auto jCrossAssets = jsonData.find("CrossChainAsset");
    if (jCrossAssets != jsonData.end()) {
        mType = TransferCrossChainAsset;
        std::vector<nlohmann::json> crossChainAssets = jsonData["CrossChainAsset"];
        uint32_t index = 0;
        for (nlohmann::json crossChainAsset : crossChainAssets) {
            CrossChainAsset* pCrossChainAsset = new CrossChainAsset(index);
            if (pCrossChainAsset) {
                pCrossChainAsset->FromJson(crossChainAsset);
                mCrossChainAssets.push_back(pCrossChainAsset);
            }
            index++;
        }
    }

}

nlohmann::json Transaction::ToJson()
{
    nlohmann::json jsonData;

    std::vector<nlohmann::json> inputs;
    for (UTXOInput* input : mInputs) {
        nlohmann::json inputJson = input->ToJson();
        inputs.push_back(inputJson);
    }
    jsonData["UTXOInputs"] = inputs;

    std::vector<nlohmann::json> outputs;
    for (TxOutput* output : mOutputs) {
        nlohmann::json outputJson = output->ToJson();
        outputs.push_back(outputJson);
    }
    jsonData["Outputs"] = outputs;

    if (mPrograms.size() > 0) {
        std::vector<nlohmann::json> programs;
        for (Program* program : mPrograms) {
            nlohmann::json programJson = program->ToJson();
            programs.push_back(programJson);
        }
        jsonData["Programs"] = programs;
    }

    if (mAttributes.size() > 0) {
        std::vector<nlohmann::json> attributes;
        for (Attribute* attribute : mAttributes) {
            nlohmann::json attrJson = attribute->ToJson();
            attributes.push_back(attrJson);
        }
        jsonData["Attributes"] = attributes;
    }

    if (mCrossChainAssets.size() > 0) {
        std::vector<nlohmann::json> crossChainAssets;
        for (CrossChainAsset* crossChainAsset : mCrossChainAssets) {
            nlohmann::json crossChainAssetJaon = crossChainAsset->ToJson();
            crossChainAssets.push_back(crossChainAssetJaon);
        }
        jsonData["CrossChainAsset"] = crossChainAssets;
    }

    return jsonData;
}

