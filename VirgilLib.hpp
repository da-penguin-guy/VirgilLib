#ifndef VIRGIL_LIB_HPP
#define VIRGIL_LIB_HPP

#include <string>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <variant>
#include <optional>

#include "nlohmann/json.hpp" // nlohmann/json single header
#include <cstdint>

// Forward declarations
struct MessageID;
struct ChannelID;
class Message;
class ChannelLink;


// Base message class that all messages inherit from
// @note Should be used as an interface
class Message {
    public:
        MessageID selfID; // Unique identifier for the message
        std::optional<MessageID> responseID; // The ID of the message this is responding to. This is not required for all messages.
        bool isOutbound; // True if message is outbound, false if inbound
        virtual nlohmann::json to_json() const = 0; // Convert message to JSON for sending
        virtual ~Message() = default; // Virtual destructor for proper cleanup

        // Automatically constructs the correct Message subclass based on the "messageType" field in the JSON.
        // @param j The JSON object to parse.
        // @param outbound True if the message is outbound, false if inbound.
        // @return A pointer to a newly allocated Message subclass instance. Caller is responsible for deleting
        static Message* FromJSON(const nlohmann::json& j, bool outbound)
        {
            if(!j.contains("messageType"))
                throw std::invalid_argument("Message JSON must contain messageType");
            std::string messageType = j.at("messageType").get<std::string>();
            if(messageType == "channelLink")
                return new ChannelLink(j, outbound);
            else if(messageType == "channelUnlink")
                return new ChannelUnlink(j, outbound);
            else
                throw std::invalid_argument("Unknown messageType: " + messageType);
        }
};

// Virgil message used to link 2 separate devices' channels together.
class ChannelLink : public Message {
public:

    ChannelID sendingChannel;
    std::optional<ChannelID> receivingChannel;
    
    /**
     * @brief Construct a ChannelLink from a JSON object.
     * @param j The JSON object to initialize from.
     * @param outbound True if the message is outbound, false if inbound.
     */
    ChannelLink(const nlohmann::json& j, bool outbound) {
        // Double checks that this is a ChannelLink message
        if(!j.contains("messageType") || j.at("messageType").get<std::string>() != "channelLink")
            throw std::invalid_argument("ChannelLink JSON must have messageType of ChannelLink");
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("ChannelLink JSON must contain messageID");
        // Reads messageID and constructs from string
        selfID = MessageID(j.at("messageID").get<std::string>());

        // Reads responseID if present. This is not required for ChannelLink messages.
        if(j.contains("responseID"))
            responseID = MessageID(j.at("responseID").get<std::string>());
        else
            responseID = MessageID(); // Empty ID if not present

        // Sets outbound status. This must be provided by the caller since the bare message does not include this information.
        isOutbound = outbound;

        // Please note that sendingChannel and receivingChannel will not be flipped based on outbound status.
        // This is because the message is from the perspective of the sender, and this class just represents the message as-is.

        // Reads sending channels with custom field names.
        sendingChannel = ChannelID(j, "sendingChannelIndex", "sendingChannelType");
        // Reads receiving channels with default field names.
        if(j.contains("channelIndex") || j.contains("channelType"))
            receivingChannel = ChannelID(j);
        else
            receivingChannel = std::nullopt;
    }

    ChannelLink(MessageID msgId, bool outbound, ChannelID sendChan, std::optional<ChannelID> recvChan, std::optional<MessageID> respId) {
        sendingChannel = sendChan;
        receivingChannel = *recvChan;
        selfID = msgId;
        isOutbound = outbound;
        responseID = *respId;
    }

    // Converts the ChannelLink to a JSON object for sending.
    // @return A JSON object representing the ChannelLink message.
    // @throws std::invalid_argument if the receivingChannel is missing when sendingChannel is not aux.
    nlohmann::json to_json() const override {
        nlohmann::json j;
        j["messageType"] = "channelLink";
        if(selfID)
            j["messageID"] = selfID.to_string();
        else {
            j["messageID"] = MessageID::GenerateNew().to_string();
        }
        if(responseID)
            j["responseID"] = responseID->to_string();
        sendingChannel.AppendJson(j, "sendingChannelIndex", "sendingChannelType");
        if(!sendingChannel.IsAux() && !receivingChannel)
            throw std::invalid_argument("Only aux sendingChannel can omit receivingChannel");
        if (receivingChannel) {
            receivingChannel->AppendJson(j);
        }
        return j;
    }


};

// Virgil message used to unlink 2 separate devices' channels.
class ChannelUnlink : public Message {
public:

    ChannelID sendingChannel;
    std::optional<ChannelID> receivingChannel;
    
    /**
     * @brief Construct a ChannelUnlink from a JSON object.
     * @param j The JSON object to initialize from.
     * @param outbound True if the message is outbound, false if inbound.
     */
    ChannelUnlink(const nlohmann::json& j, bool outbound) {
        // Double checks that this is a ChannelUnlink message
        if(!j.contains("messageType") || j.at("messageType").get<std::string>() != "channelUnlink")
            throw std::invalid_argument("ChannelUnlink JSON must have messageType of ChannelUnlink");
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("ChannelUnlink JSON must contain messageID");
        // Reads messageID and constructs from string
        selfID = MessageID(j.at("messageID").get<std::string>());

        // Reads responseID if present. This is not required for ChannelUnlink messages.
        if(j.contains("responseID"))
            responseID = MessageID(j.at("responseID").get<std::string>());
        else
            responseID = MessageID(); // Empty ID if not present

        // Sets outbound status. This must be provided by the caller since the bare message does not include this information.
        isOutbound = outbound;

        // Please note that sendingChannel and receivingChannel will not be flipped based on outbound status.
        // This is because the message is from the perspective of the sender, and this class just represents the message as-is.

        // Reads sending channels with custom field names.
        sendingChannel = ChannelID(j, "sendingChannelIndex", "sendingChannelType");
        // Reads receiving channels with default field names.
        if(j.contains("channelIndex") || j.contains("channelType"))
            receivingChannel = ChannelID(j);
        else
            receivingChannel = std::nullopt;
    }

    ChannelUnlink(MessageID msgId, bool outbound, ChannelID sendChan, std::optional<ChannelID> recvChan, std::optional<MessageID> respId) {
        sendingChannel = sendChan;
        receivingChannel = *recvChan;
        selfID = msgId;
        isOutbound = outbound;
        responseID = *respId;
    }

    // Converts the ChannelUnlink to a JSON object for sending.
    // @return A JSON object representing the ChannelUnlink message.
    // @throws std::invalid_argument if the receivingChannel is missing when sendingChannel is not aux.
    nlohmann::json to_json() const override {
        nlohmann::json j;
        j["messageType"] = "channelUnlink";
        if(selfID)
            j["messageID"] = selfID.to_string();
        else {
            j["messageID"] = MessageID::GenerateNew().to_string();
        }
        sendingChannel.AppendJson(j, "sendingChannelIndex", "sendingChannelType");
        if(!sendingChannel.IsAux() && !receivingChannel)
            throw std::invalid_argument("Only aux sendingChannel can omit receivingChannel");
        if (receivingChannel) {
            receivingChannel->AppendJson(j);
        }
        if(responseID)
            j["responseID"] = responseID->to_string();
        return j;
    }


};

// Virgil message used to indicate the end of a response sequence.
class EndResponse : public Message {
public:
    // Constructs an EndResponse from a JSON object.
    EndResponse(const nlohmann::json& j, bool outbound) {
        // Double checks that this is an EndResponse message
        if(!j.contains("messageType") || j.at("messageType").get<std::string>() != "endResponse")
            throw std::invalid_argument("EndResponse JSON must have messageType of EndResponse");
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("EndResponse JSON must contain messageID");
        if(!j.contains("responseID"))
            throw std::invalid_argument("EndResponse JSON must contain responseID");
        // Reads messageID and constructs from string
        selfID = MessageID(j.at("messageID").get<std::string>());
        responseID = MessageID(j.at("responseID").get<std::string>());

        // Sets outbound status. This must be provided by the caller since the bare message does not include this information.
        isOutbound = outbound;
    }

    // Constructs an EndResponse with given parameters.
    EndResponse(MessageID msgId, bool outbound, MessageID respId) {
        responseID = respId;
        selfID = msgId;
        isOutbound = outbound;
    }

    // Converts the EndResponse to a JSON object for sending.
    nlohmann::json to_json() const override {
        nlohmann::json j;
        j["messageType"] = "endResponse";
        if(selfID)
            j["messageID"] = selfID.to_string();
        else {
            j["messageID"] = MessageID::GenerateNew().to_string();
        }
        j["responseID"] = responseID->to_string();
        return j;
    }
};

// Virgil message used to indicate an error in a response sequence.
class ErrorResponse : public Message {
public:
    MessageID responseID; // The ID of the message this is responding to
    std::string errorValue; // The predefined error type
    std::string errorString; // Human-readable error message

    // Constructs an ErrorResponse from a JSON object.
    ErrorResponse(const nlohmann::json& j, bool outbound) {
        // Double checks that this is an ErrorResponse message
        if(!j.contains("messageType") || j.at("messageType").get<std::string>() != "errorResponse")
            throw std::invalid_argument("ErrorResponse JSON must have messageType of ErrorResponse");
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("ErrorResponse JSON must contain messageID");
        if(!j.contains("responseID"))
            throw std::invalid_argument("ErrorResponse JSON must contain responseID");
        if(!j.contains("errorValue"))
            throw std::invalid_argument("ErrorResponse JSON must contain errorValue");
        if(!j.contains("errorString"))
            throw std::invalid_argument("ErrorResponse JSON must contain errorString");

        // Reads messageID and constructs from string
        selfID = MessageID(j.at("messageID").get<std::string>());
        responseID = MessageID(j.at("responseID").get<std::string>());
        // Reads errorValue and errorString
        errorValue = j.at("errorValue").get<std::string>();
        errorString = j.at("errorString").get<std::string>();
        // Sets outbound status. This must be provided by the caller since the bare message does not include this information.
        isOutbound = outbound;
    }

    // Constructs an ErrorResponse with given parameters.
    ErrorResponse(MessageID msgId, bool outbound, MessageID respId, const std::string& errorVal, const std::string& errorStr) {
        responseID = respId;
        selfID = msgId;
        isOutbound = outbound;
        errorValue = errorVal;
        errorString = errorStr;
    }

    // Constructs an ErrorResponse with given parameters. Does not have a messageID; one will be generated when sending.
    ErrorResponse(bool outbound, MessageID respId, const std::string& errorVal, const std::string& errorStr) {
        responseID = respId;
        selfID = MessageID();  // Empty ID for new error messages
        isOutbound = outbound;
        errorValue = errorVal;
        errorString = errorStr;
    }

    // Converts the ErrorResponse to a JSON object for sending.
    nlohmann::json to_json() const override {
        nlohmann::json j;
        j["messageType"] = "errorResponse";
        if(selfID)
            j["messageID"] = selfID.to_string();
        else {
            j["messageID"] = MessageID::GenerateNew().to_string();
        }
        j["responseID"] = responseID.to_string();
        j["errorValue"] = errorValue;
        j["errorString"] = errorString;
        return j;
    }
};

// Virgil message used to request information about a channel.
class InfoRequest : public Message {
public:
    ChannelID channel; // The channel to request info about
    // Constructs an InfoRequest from a JSON object.
    InfoRequest(const nlohmann::json& j, bool outbound) {
        // Double checks that this is an InfoRequest message
        if(!j.contains("messageType") || j.at("messageType").get<std::string>() != "infoRequest")
            throw std::invalid_argument("InfoRequest JSON must have messageType of InfoRequest");
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("InfoRequest JSON must contain messageID");

        // Reads messageID and constructs from string
        selfID = MessageID(j.at("messageID").get<std::string>());

        channel = ChannelID(j);

        // Reads responseID if present. This is not required for InfoRequest messages.
        if(j.contains("responseID"))
            responseID = MessageID(j.at("responseID").get<std::string>());

        // Sets outbound status. This must be provided by the caller since the bare message does not include this information.
        isOutbound = outbound;
    }

    // Constructs an InfoRequest with given parameters.
    InfoRequest(const MessageID& msgId, bool outbound, const ChannelID& channelId, std::optional<MessageID> respId) {
        responseID = *respId;
        selfID = msgId;
        isOutbound = outbound;
        channel = channelId;
    }

    // Constructs an InfoRequest with given parameters. Does not have a messageID; one will be generated when sending.
    InfoRequest(bool outbound, const ChannelID& channelId, std::optional<MessageID> respId) {
        responseID = *respId;
        selfID = MessageID();  // Empty ID
        isOutbound = outbound;
        channel = channelId;
    }

    // Converts the InfoRequest to a JSON object for sending.
    nlohmann::json to_json() const override {
        nlohmann::json j;
        j["messageType"] = "infoRequest";
        if(selfID)
            j["messageID"] = selfID.to_string();
        else 
            j["messageID"] = MessageID::GenerateNew().to_string();

        if(responseID)
            j["responseID"] = responseID->to_string();

        if(!channel)
            throw std::invalid_argument("InfoRequest must have a valid channel");
        channel.AppendJson(j);
        return j;
    }
};

class InfoResponse : public Message {
    ChannelID channel; // The channel to request info about
    // Constructs an InfoResponse from a JSON object.
    InfoResponse(const nlohmann::json& j, bool outbound) {
        // Double checks that this is an InfoResponse message
        if(!j.contains("messageType") || j.at("messageType").get<std::string>() != "infoResponse")
            throw std::invalid_argument("InfoResponse JSON must have messageType of InfoResponse");
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("InfoResponse JSON must contain messageID");
        // Reads responseID if present. This is required for InfoResponse messages.
        if(!j.contains("responseID"))
            throw std::invalid_argument("InfoResponse JSON must contain responseID");

        // Reads messageID and constructs from string
        selfID = MessageID(j.at("messageID").get<std::string>());
        responseID = MessageID(j.at("responseID").get<std::string>());

        channel = ChannelID(j);

        if(channel.IsDeviceLevel()){
            if(!j.contains("model"))
                throw std::invalid_argument("InfoResponse JSON for device-level channel must contain model");
            if(!j.contains("virgilVersion"))
                throw std::invalid_argument("InfoResponse JSON for device-level channel must contain virgilVersion");
            if(!j.contains("deviceType"))
                throw std::invalid_argument("InfoResponse JSON for device-level channel must contain deviceType");
            if(!j.contains("channelCounts"))
                throw std::invalid_argument("InfoResponse JSON for device-level channel must contain channelCounts");
        }
        else{

        }

        // Sets outbound status. This must be provided by the caller since the bare message does not include this information.
        isOutbound = outbound;
    }

    // Converts the InfoRequest to a JSON object for sending.
    nlohmann::json to_json() const override {
        nlohmann::json j;
        j["messageType"] = "infoRequest";
        if(selfID)
            j["messageID"] = selfID.to_string();
        else 
            j["messageID"] = MessageID::GenerateNew().to_string();

        if(responseID)
            j["responseID"] = responseID->to_string();

        if(!channel)
            throw std::invalid_argument("InfoRequest must have a valid channel");
        channel.AppendJson(j);
        return j;
    }
};

// @brief A struct representing a parameter with various attributes and validation.
struct Parameter {
    std::string name; // Parameter name
    std::string dataType; // "number", "bool", "string", or "enum"
    std::optional<std::string> unit; // Unit of measurement. Shorthand like "dB" or "Hz"
    std::variant<int,double,bool,std::string> value; // Current value
    std::optional<std::variant<int,double>> minValue; // Minimum value for number types
    std::optional<std::variant<int,double>> maxValue; // Maximum value for number types
    std::optional<std::variant<int,double>> precision; // Precision for number types
    bool readOnly; // True if parameter is read-only

    // Constructor for string parameters
    Parameter(const std::string& paramName, const std::string& paramValue, bool isReadOnly)
    {
        if(paramName.empty())
            throw std::invalid_argument("Parameter name cannot be empty");
        name = paramName;
        dataType = "string";
        value = paramValue;
        readOnly = isReadOnly;
    }

    // Constructor for VirgilEnum parameters. These are just strings with a predefined set of valid values.
    Parameter(const std::string& paramName, const VirgilEnum& paramValue, bool isReadOnly)
    {
        if(paramName.empty())
            throw std::invalid_argument("Parameter name cannot be empty");
        if(!paramValue)
            throw std::invalid_argument("Invalid enum value");
        name = paramName;
        dataType = "enum";
        value = paramValue;
        readOnly = isReadOnly;
    }

    // Constructor for int parameters
    Parameter(const std::string& paramName, int paramValue, bool isReadOnly, std::optional<int> minVal, std::optional<int> maxVal, std::optional<int> prec)
    {
        if(paramName.empty())
            throw std::invalid_argument("Parameter name cannot be empty");
        if((minVal && maxVal) && (*minVal > *maxVal))
            throw std::invalid_argument("minValue cannot be greater than maxValue");
        if(prec && *prec <= 0)
            throw std::invalid_argument("precision must be greater than 0");
        if(!isReadOnly && (!minVal || !maxVal || !prec))
            throw std::invalid_argument("Non-readonly parameters must have minValue, maxValue, and precision");
        if(!isReadOnly && minVal && maxVal && prec) {
            if((paramValue - *minVal) % *prec != 0 || paramValue < *minVal || paramValue > *maxVal)
                throw std::invalid_argument("Initial value is not valid according to the formula");
        }
        name = paramName;
        dataType = "int";
        value = paramValue;
        if(minValue)
            minValue = minVal;
        if(maxValue)
            maxValue = maxVal;
        if(precision)
            precision = prec;
        readOnly = isReadOnly;
    }

    // Constructor for double parameters
    Parameter(const std::string& paramName, double paramValue, bool isReadOnly, std::optional<double> minVal, std::optional<double> maxVal, std::optional<double> prec)
    {
        if(paramName.empty())
            throw std::invalid_argument("Parameter name cannot be empty");
        if((minVal && maxVal) && (*minVal > *maxVal))
            throw std::invalid_argument("minValue cannot be greater than maxValue");
        if(prec && *prec <= 0)
            throw std::invalid_argument("precision must be greater than 0");
        if(!isReadOnly && (!minVal || !maxVal || !prec))
            throw std::invalid_argument("Non-readonly parameters must have minValue, maxValue, and precision");
        name = paramName;
        dataType = "int";
        value = paramValue;
        if(minValue)
            minValue = minVal;
        if(maxValue)
            maxValue = maxVal;
        if(precision)
            precision = prec;
        readOnly = isReadOnly;
    }

    // Constructor for bool parameters
    Parameter(const std::string& paramName, const bool paramValue, bool isReadOnly)
    {
        if(paramName.empty())
            throw std::invalid_argument("Parameter name cannot be empty");
        name = paramName;
        dataType = "bool";
        value = paramValue;
        readOnly = isReadOnly;
    }

    // Converts the Parameter to a JSON object for sending. This does not contain the name field.
    nlohmann::json to_json() const {

        if(name.empty())
            throw std::invalid_argument("Parameter name cannot be empty");
        
            

        nlohmann::json j;
        j["dataType"] = dataType;

        std::visit([&j](auto&& arg) { j["value"] = arg; }, value);

        j["readOnly"] = readOnly;
        if(unit)
            j["unit"] = *unit;
        if(minValue)
            std::visit([&j](auto&& arg) { j["minValue"] = arg; }, *minValue);
        if(maxValue)
            std::visit([&j](auto&& arg) { j["maxValue"] = arg; }, *maxValue);
        if(precision)
            std::visit([&j](auto&& arg) { j["precision"] = arg; }, *precision);
        return j;
    }

    // Appends the Parameter to an existing JSON object with the name as the key.
    void append_json(nlohmann::json& j) const {
        j[name] = to_json();
    }

    // Validates the Parameter's fields and returns true if valid, false otherwise.
    operator bool() const {
        // Checks if name is not empty
        if(name.empty())
            return false;

        if(dataType == "enum") {
            // Check if value holds a VirgilEnum
            if(!std::holds_alternative<VirgilEnum>(value))
                return false;
            // Check if the enum is valid
            VirgilEnum enumVal = std::get<VirgilEnum>(value);
            if(!enumVal)
                return false;
        }
        else if(dataType == "number")
        {
            // Check if value holds int or double
            if(!std::holds_alternative<int>(value) && !std::holds_alternative<double>(value))
                return false;

            // If not readOnly, minValue, maxValue, and precision must be set
            if(!readOnly) {
                // Makes sure they're set
                if(!minValue || !maxValue || !precision)
                    return false;

                // Check type consistency: if value is int, min/max/precision should also be int
                if(std::holds_alternative<int>(value)) {
                    if(!std::holds_alternative<int>(*minValue) || 
                       !std::holds_alternative<int>(*maxValue) || 
                       !std::holds_alternative<int>(*precision))
                        return false;
                } else if(std::holds_alternative<double>(value)) {
                    if(!std::holds_alternative<double>(*minValue) || 
                       !std::holds_alternative<double>(*maxValue) || 
                       !std::holds_alternative<double>(*precision))
                        return false;
                }
            }
        }
        else if (dataType == "bool") {
            // Check if value holds a bool
            if(!std::holds_alternative<bool>(value))
                return false;
        }
        else if (dataType == "string") {
            // Check if value holds a string
            if(!std::holds_alternative<std::string>(value))
                return false;
        }
        else
            return false; // Invalid dataType
        return true;
    }
};


struct VirgilEnum {
    std::string value; // Current value
    std::vector<std::string> enumValues; // List of valid enum values

    // Default constructor. This will be considered invalid until properly initialized.
    VirgilEnum() = default;

    // Constructor with value and enum values
    VirgilEnum(std::string val, std::vector<std::string> enumVals)
    {
        value = val;
        enumValues = enumVals;
    }

    // Validates the VirgilEnum's fields and returns true if valid, false otherwise.
    operator bool() const {
        // Checks if value is not empty
        if(enumValues.size() < 1)
            return false;

        // Checks if value is in enumValues
        for(const auto& v : enumValues)
        {
            if(v == value)
                // Exits during the loop if a match is found
                return true;
        }
        return false;
    }

    bool operator==(const VirgilEnum& other) const {
        if(!this || !other)
            std::invalid_argument("Cannot compare invalid VirgilEnums");
        return value == other.value && enumValues == other.enumValues;
    }

    bool operator!=(const VirgilEnum& other) const {
        return !(*this == other);
    }

};

// @brief A struct representing a unique channel identifier, consisting of a channel index and type.
// @details Channel index is -1 for device-level info, otherwise >= 0. Channel type is a string like "aux", "tx", or "rx".
// @note It is not recommended to read the channel index or type directly; use the provided accessors instead.
struct ChannelID {
    std::optional<int> channelIndex; // Index of the channel, nullopt if not set
    std::string channelType; // Type of the channel, e.g. "aux", "tx", "rx", "device"

    ChannelID() : channelIndex(std::nullopt), channelType("") {}

    // Constructor for when both channel index and type are provided
    ChannelID(const int index, const std::string& type)
    {
        if(index < 0)
            throw std::invalid_argument("Channel index out of range.");
        if(type.empty())
            throw std::invalid_argument("Channel type cannot be empty");
        channelType = type;
        channelIndex = index;
    }

    // Constructor for when only channel type is provided (index will be set to nullopt to indicate no value)
    ChannelID(const std::string& type)
    {
        if (type.empty())
            throw std::invalid_argument("Channel type cannot be empty");
        if (type != "device")
            throw std::invalid_argument("Only device channel type can be used without an index");
        channelType = type;
        channelIndex = std::nullopt; // Set to nullopt to indicate no value
    }

    // Scans a JSON object for channelIndex and channelType, and constructs a ChannelID.
    ChannelID(const nlohmann::json& j) : ChannelID(j, "channelIndex", "channelType") {}

    // Scans a JSON object for channelIndex and channelType with custom field names, and constructs a ChannelID.
    ChannelID(const nlohmann::json& j, const std::string& channelindexName, const std::string& channelTypeName) : ChannelID(
        j.contains(channelindexName) && j.contains(channelTypeName) ?
            ChannelID(j.at(channelindexName).get<int>(), j.at(channelTypeName).get<std::string>()) :
        j.contains(channelTypeName) ?
            ChannelID(j.at(channelTypeName).get<std::string>()) :
            throw std::invalid_argument("ChannelID JSON must contain at least channelType")
    ) {}

    // Converts the ChannelID to a JSON object with field names of "channelIndex" and "channelType".
    // @return A JSON object containing only "channelIndex" and "channelType" fields if they are set.
    nlohmann::json to_json() const {
        return to_json("channelIndex", "channelType");
    }

    // Converts the ChannelID to a JSON object with custom field names.
    // @param channelindexName The field name to store the channel index in.
    // @param channelTypeName The field name to store the channel type in.
    // @return A JSON object containing only the specified fields if they are set.
    nlohmann::json to_json(const std::string& channelindexName, const std::string& channelTypeName) const {
        nlohmann::json j;
        if (channelIndex)
            j[channelindexName] = *channelIndex;
        if (!channelType.empty())
            j[channelTypeName] = channelType;
        return j;
    }

    // Appends the ChannelID fields to an existing JSON object with custom field names.
    // @param j The JSON object to append to. It does not clear existing fields, but will overwrite the fields if they already exist.
    // @param channelindexName The field name to store the channel index in.
    // @param channelTypeName The field name to store the channel type in.
    void AppendJson(nlohmann::json& j, const std::string& channelindexName, const std::string& channelTypeName) const {
        if (channelIndex)
            j[channelindexName] = *channelIndex;
        if (!channelType.empty())
            j[channelTypeName] = channelType;
    }

    // Appends the ChannelID fields to an existing JSON object with field names of "channelIndex" and "channelType".
    // @param j The JSON object to append to. It does not clear existing fields, but will overwrite the fields if they already exist.
    void AppendJson(nlohmann::json& j) const {
        AppendJson(j, "channelIndex", "channelType");
    }

    // Checks if the channel is a device-level channel.
    bool IsDevice() const {
        return channelType == "device";
    }

    // Checks if the channel is an auxiliary channel.
    bool IsAux() const {
        return channelType == "aux";
    }

    // Checks if the channel is a transmit channel.
    bool IsTx() const {
        return channelType == "tx";
    }

    // Checks if the channel is a receive channel.
    bool IsRx() const {
        return channelType == "rx";
    }

    // Check if this ChannelID represents a device-level info (no specific channel index)
    bool IsDeviceLevel() const {
        return channelType == "device" && !channelIndex;
    }

    // Gets the index of the channel.
    // @return The channel index.
    // @throws std::runtime_error if the channel is device-level (no index) or if the index is not set.
    int GetIndex() const {
        if (IsDevice())
            throw std::runtime_error("Device channel does not have an index");
        if (!channelIndex)
            throw std::runtime_error("Channel index is not set");
        return *channelIndex;
    }

    operator bool() const {
        if(channelType.empty())
            return false; // Channel type must be set
        if (IsDevice() && channelIndex)
            return false; // Device channel should not have an index
        if (!IsDevice() && (!channelIndex || *channelIndex < 0))
            return false; // Non-device channel must have an index greater then 0
        return true;
    }


    bool operator==(const ChannelID& other) const {
        return channelIndex == other.channelIndex && channelType == other.channelType;
    }
    bool operator!=(const ChannelID& other) const {
        return !(*this == other);
    }
};

struct LinkedChannelInfo {
    std::string deviceName;
    ChannelID channel;

    // Default constructor. Will be considered invalid until properly initialized.
    LinkedChannelInfo() = default;

    // Constructor with device name and channel
    LinkedChannelInfo(const std::string& devName, const ChannelID& chan) : deviceName(devName), channel(chan) {
        if(devName.empty())
            throw std::invalid_argument("Device name cannot be empty");
        if(!chan)
            throw std::invalid_argument("Invalid ChannelID");
    }

    // Constructor from JSON
    LinkedChannelInfo(const nlohmann::json& j) {
        // Checks for deviceName field
        if(!j.contains("deviceName"))
            throw std::invalid_argument("LinkedChannelInfo JSON must contain deviceName");
        deviceName = j.at("deviceName").get<std::string>();

        // Creates ChannelID from JSON
        channel = ChannelID(j);

        // Checks validity
        if(!*this)
            throw std::invalid_argument("Invalid LinkedChannelInfo");
    }

    nlohmann::json to_json() const {
        if(!*this)
            throw std::invalid_argument("Invalid LinkedChannelInfo");
        nlohmann::json j;
        j["deviceName"] = deviceName;
        channel.AppendJson(j);
        return j;
    }

    operator bool() const {
        return !deviceName.empty() && channel;
    }
};

/** 
 * @brief A struct representing a unique message identifier. Contains the time the message was sent and an arbitrary index.
 */
struct MessageID {
    static std::chrono::system_clock::time_point timeLastSent;
    static int currentMessageIndex;

    static MessageID GenerateNew() {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

        if (now == timeLastSent) {
            currentMessageIndex++;
        } else {
            currentMessageIndex = 0;
            timeLastSent = now;
        }
        MessageID newId;
        newId.timeSent = now;
        newId.messageIndex = currentMessageIndex;
        return newId;
    }

    // Time the message was sent (system_clock::time_point)
    std::chrono::system_clock::time_point timeSent{};
    // Index of the message within the same millisecond (0-999).
    int messageIndex = 0; 

    // Default constructor
    MessageID() = default; 
 
    // @brief Constructor from format provided in virgil messages
    // @param id A 12-digit string in the format HHMMSSmmm### where HH is hours, MM is minutes, SS is seconds, mmm is milliseconds, and ### is the message index.
    MessageID(const std::string& id) {
        if (id.length() != 12)
            throw std::invalid_argument("MessageId must be 12 digits");

        // Get last 3 digits as message index
        messageIndex = static_cast<int>(std::stoi(id.substr(9, 3)));

    // Parse time
    std::chrono::milliseconds total_ms = std::chrono::hours(std::stoi(id.substr(0, 2)))
              + std::chrono::minutes(std::stoi(id.substr(2, 2)))
              + std::chrono::seconds(std::stoi(id.substr(4, 2)))
              + std::chrono::milliseconds(std::stoi(id.substr(6, 3)));
    // Set timeSent as a time_point since midnight (today)
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time);
    std::chrono::system_clock::time_point midnight = std::chrono::system_clock::from_time_t(std::mktime(now_tm));
    timeSent = midnight + total_ms;
    }

    // Convert to string to be used in virgil messages
    // @return HHMMSSmmm### format
    std::string to_string() const {
        // Calculate ms since midnight for this timeSent
        std::time_t time_time = std::chrono::system_clock::to_time_t(timeSent);
        std::tm* time_tm = std::localtime(&time_time);
        std::chrono::system_clock::time_point midnight = std::chrono::system_clock::from_time_t(std::mktime(time_tm));
        std::chrono::milliseconds ms_since_midnight = std::chrono::duration_cast<std::chrono::milliseconds>(timeSent - midnight);
        long long ms = ms_since_midnight.count();
        int hour = static_cast<int>(ms / 3600000);
        ms = ms % 3600000;
        int minute = static_cast<int>(ms / 60000);
        ms = ms % 60000;
        int second = static_cast<int>(ms / 1000);
        ms = ms % 1000;
        int millisecond = static_cast<int>(ms);
        std::ostringstream oss;
        oss << std::setw(2) << std::setfill('0') << hour
            << std::setw(2) << std::setfill('0') << minute
            << std::setw(2) << std::setfill('0') << second
            << std::setw(3) << std::setfill('0') << millisecond
            << std::setw(3) << std::setfill('0') << messageIndex;
        return oss.str();
    }

    bool operator==(const MessageID& other) const {
        return timeSent == other.timeSent && messageIndex == other.messageIndex;
    }
    bool operator!=(const MessageID& other) const {
        return !(*this == other);
    }

    operator bool() const {
    // Consider it valid if timeSent is not epoch or messageIndex is not zero
    return timeSent != std::chrono::system_clock::time_point{} || messageIndex != 0;
    }

};

#endif
