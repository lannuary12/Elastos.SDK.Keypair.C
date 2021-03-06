
#include "Attribute.h"

void Attribute::Serialize(ByteStream& ostream)
{
    ostream.put(mUsage);

    ostream.putVarUint(mData.GetSize());
    ostream.putBytes(mData, mData.GetSize());
}

void Attribute::FromJson(const nlohmann::json &jsonData)
{
    mUsage = jsonData["usage"].get<Usage>();
    mData = Utils::decodeHex(jsonData["data"].get<std::string>());
}

nlohmann::json Attribute::ToJson()
{
    nlohmann::json jsonData;
    jsonData["usage"] = mUsage;
    jsonData["data"] = Utils::encodeHex(mData);

    return jsonData;
}
