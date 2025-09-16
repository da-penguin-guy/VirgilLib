#ifndef VIRGIL_LIB_HPP
#define VIRGIL_LIB_HPP

#include <string>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <variant>
#include <optional>
#include <cstdint>

#include "nlohmann/json.hpp" // nlohmann/json single header
#include <cstdint>

// Forward declarations
struct MessageID;
struct ChannelID;
class Message;
class ChannelLink;


/**
 * @brief Base abstract class for all Virgil Protocol 2.3.0 messages.
 * 
 * This class serves as the foundation for all message types in the Virgil network protocol
 * for controlling audio devices using JSON-formatted messages over TCP. All Virgil messages
 * must contain a unique messageID and may optionally contain a responseID when responding
 * to another message.
 * 
 * The Virgil protocol supports the following message types:
 * - channelLink/channelUnlink: Link/unlink channels between devices
 * - infoRequest/infoResponse: Request/respond with channel information
 * - deviceInfoRequest/deviceInfoResponse: Request/respond with device-level information
 * - parameterCommand: Set or change a parameter on a channel
 * - statusUpdate: Notify devices of changed parameters
 * - statusRequest: Request status update for a channel
 * - subscribeMessage/unsubscribeMessage: Manage parameter change subscriptions
 * - errorResponse: Convey errors in communication
 * - endResponse: Indicate end of response sequence
 * 
 * @note This class should be used as an interface - all derived classes must implement to_json()
 * @see Virgil Protocol 2.3.0 specification for complete message format details
 */
class Message {
    public:
        MessageID selfID; // Unique identifier for the message
        std::optional<MessageID> responseID; // The ID of the message this is responding to. This is not required for all messages.
        bool isOutbound; // True if message is outbound, false if inbound
        virtual nlohmann::json to_json() const = 0; // Convert message to JSON for sending
        virtual ~Message() = default; // Virtual destructor for proper cleanup

        /**
         * @brief Factory method to construct the appropriate Message subclass from JSON.
         * 
         * Automatically parses the "messageType" field in the JSON and constructs the
         * corresponding Message subclass instance. This is the primary method for
         * deserializing incoming Virgil protocol messages.
         * 
         * Supported message types:
         * - "channelLink" -> ChannelLink instance
         * - "channelUnlink" -> ChannelUnlink instance
         * 
         * @param j The JSON object containing the message data. Must include "messageType" field.
         * @param outbound True if the message is outbound (being sent), false if inbound (received).
         * @return A pointer to a newly allocated Message subclass instance. 
         * @throws std::invalid_argument if messageType is missing or unknown
         * @note Caller is responsible for deleting the returned pointer
         * @note Additional message types will be added as they are implemented
         * 
         * @example
         * ```cpp
         * nlohmann::json msg_json = // ... received JSON message
         * Message* msg = Message::FromJSON(msg_json, false); // false = inbound
         * // Use the message...
         * delete msg; // Don't forget to cleanup
         * ```
         */
        static Message* FromJSON(const nlohmann::json& j, bool outbound)
        {
            if(!j.contains("messageType"))
                throw std::invalid_argument("Message JSON must contain 'messageType' field. Received JSON keys: " + 
                    [&j]() {
                        std::string keys = "";
                        for (auto it = j.begin(); it != j.end(); ++it) {
                            if (!keys.empty()) keys += ", ";
                            keys += "'" + it.key() + "'";
                        }
                        return keys.empty() ? "(empty)" : keys;
                    }());
            
            std::string messageType = j.at("messageType").get<std::string>();
            if(messageType == "channelLink")
                return new ChannelLink(j, outbound);
            else if(messageType == "channelUnlink")
                return new ChannelUnlink(j, outbound);
            else
                throw std::invalid_argument("Unknown messageType value: '" + messageType + "'. Supported types: 'channelLink', 'channelUnlink'");
        }
};

/**
 * @brief Virgil Protocol message for linking channels between devices.
 * 
 * The channelLink message is used to represent that 2 channels are linked in Dante audio
 * networking. This message establishes the audio flow relationship between a sending channel
 * and a receiving channel across different devices.
 * 
 * Channel linking rules according to Virgil Protocol 2.3.0:
 * - TX (transmit) channels can only be linked to RX (receive) channels and vice versa
 * - AUX channels are linked to devices, not other channels
 * - Either side can initiate the link by sending a channelLink message
 * - Linking automatically subscribes each device to the corresponding channel
 * - If channels are subscribed in Dante, they must be linked in Virgil
 * 
 * For AUX channels, the receivingChannel can be omitted as they link to devices rather
 * than specific channels. The linking must be initiated by the device not containing 
 * the aux channel.
 * 
 * @see Virgil Protocol 2.3.0 - "Linking Channels" section
 * @see ChannelUnlink for the corresponding unlink operation
 */
class ChannelLink : public Message {
public:

    ChannelID sendingChannel;
    std::optional<ChannelID> receivingChannel;
    
    /**
     * @brief Construct a ChannelLink from a JSON object received over the network.
     * 
     * Parses a JSON message containing channelLink data and initializes the object.
     * Validates required fields and constructs appropriate ChannelID objects for
     * sending and receiving channels.
     * 
     * Required JSON fields:
     * - "messageType": must be "channelLink"
     * - "messageID": 12-digit Virgil message ID
     * - "sendingChannelIndex": index of the sending channel
     * - "sendingChannelType": type of sending channel (0=tx, 1=rx, 2=aux)
     * 
     * Optional JSON fields:
     * - "responseID": ID of message this responds to
     * - "channelIndex": receiving channel index (omitted for aux channels)
     * - "channelType": receiving channel type (omitted for aux channels)
     * 
     * @param j The JSON object to initialize from. Must be valid Virgil channelLink message.
     * @param outbound True if the message is outbound (being sent), false if inbound (received).
     * @throws std::invalid_argument if required fields are missing or messageType is incorrect
     * @note For AUX channels, receivingChannel will be std::nullopt as they link to devices
     */
    ChannelLink(const nlohmann::json& j, bool outbound) {
        // Double checks that this is a ChannelLink message
        if(!j.contains("messageType"))
            throw std::invalid_argument("ChannelLink JSON must contain 'messageType' field");
        
        std::string msgType = j.at("messageType").get<std::string>();
        if(msgType != "channelLink")
            throw std::invalid_argument("ChannelLink JSON must have messageType='channelLink', but received messageType='" + msgType + "'");
        
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("ChannelLink JSON must contain 'messageID' field");
        // Reads messageID and constructs from string
        selfID = MessageID(j.at("messageID").get<std::string>());

        // Reads responseID if present. This is not required for ChannelLink messages per Virgil protocol.
        if(j.contains("responseID"))
            responseID = MessageID(j.at("responseID").get<std::string>());
        else
            responseID = MessageID(); // Empty ID if not present

        // Sets outbound status. This must be provided by the caller since the bare message does not include this information.
        isOutbound = outbound;

        // NOTE: sendingChannel and receivingChannel will not be flipped based on outbound status.
        // This is because the message is from the perspective of the sender, and this class just represents the message as-is.
        // The semantics are: sendingChannel is transmitting data TO the receivingChannel

        // Reads sending channel information with custom field names as per Virgil protocol specification
        // Uses "sendingChannelIndex" and "sendingChannelType" to distinguish from receiving channel fields
        sendingChannel = ChannelID(j, "sendingChannelIndex", "sendingChannelType");
        
        // Reads receiving channel information with standard field names ("channelIndex", "channelType")
        // For AUX channels, receiving channel may be omitted as they link to devices rather than channels
        if(j.contains("channelIndex") || j.contains("channelType"))
            receivingChannel = ChannelID(j);
        else
            receivingChannel = std::nullopt; // AUX channels can omit receiving channel
    }

    /**
     * @brief Construct a ChannelLink programmatically with specified parameters.
     * 
     * Creates a ChannelLink message for establishing a connection between channels.
     * This constructor is used when creating new link requests programmatically
     * rather than parsing from received JSON.
     * 
     * @param msgId The message ID for this channelLink message
     * @param outbound True if the message is outbound (being sent), false if inbound
     * @param sendChan The channel that is sending/transmitting data  
     * @param recvChan The channel that is receiving data (std::nullopt for AUX channels)
     * @param respId Optional response ID if this is responding to another message
     * @note For AUX channels, recvChan should be std::nullopt as they link to devices
     */
    ChannelLink(MessageID msgId, bool outbound, ChannelID sendChan, std::optional<ChannelID> recvChan, std::optional<MessageID> respId) {
        sendingChannel = sendChan;
        receivingChannel = *recvChan;
        selfID = msgId;
        isOutbound = outbound;
        responseID = *respId;
    }

    /**
     * @brief Converts the ChannelLink to a JSON object for network transmission.
     * 
     * Serializes the ChannelLink message into the standard Virgil protocol JSON format
     * for sending over TCP. Validates that the message is properly formed according
     * to protocol requirements.
     * 
     * Generated JSON structure:
     * ```json
     * {
     *   "messageType": "channelLink",
     *   "messageID": "143052847000",
     *   "sendingChannelIndex": 0,
     *   "sendingChannelType": 0,
     *   "channelIndex": 1,        // omitted for AUX channels
     *   "channelType": 1,         // omitted for AUX channels  
     *   "responseID": "..."       // optional
     * }
     * ```
     * 
     * @return A JSON object representing the ChannelLink message ready for transmission
     * @throws std::invalid_argument if receivingChannel is missing when sendingChannel is not AUX
     * @note Automatically generates a new messageID if selfID is empty
     * @note AUX channels can omit receivingChannel as they link to devices, not channels
     */
    nlohmann::json to_json() const override {
        nlohmann::json j;
        j["messageType"] = "channelLink";
        
        // Generate messageID if not already set, or use existing one
        if(selfID)
            j["messageID"] = selfID.to_string();
        else {
            j["messageID"] = MessageID::GenerateNew().to_string();
        }
        
        // Include responseID only if this message is responding to another message
        if(responseID)
            j["responseID"] = responseID->to_string();
            
        // Add sending channel info with custom field names to distinguish from receiving channel
        sendingChannel.AppendJson(j, "sendingChannelIndex", "sendingChannelType");
        
        // Validate Virgil protocol rule: only AUX channels can omit receiving channel
        // TX/RX channels must specify both sending and receiving channels for audio flow
        if(!sendingChannel.IsAux() && !receivingChannel)
            throw std::invalid_argument("Non-AUX sendingChannel (type=" + std::to_string(static_cast<int>(sendingChannel.channelType)) + 
                ", index=" + std::to_string(sendingChannel.channelIndex) + ") must have a receivingChannel. Only AUX channels can omit receivingChannel.");
        
        // Add receiving channel info with standard field names (channelIndex, channelType) if present
        if (receivingChannel) {
            receivingChannel->AppendJson(j);
        }
        return j;
    }


};

/**
 * @brief Virgil Protocol message for unlinking previously linked channels between devices.
 * 
 * The channelUnlink message is used to break the connection between two previously linked
 * channels in the Dante audio network. This is the reverse operation of channelLink and
 * removes the audio flow relationship between devices.
 * 
 * Unlinking behavior:
 * - Breaks the established link between sending and receiving channels
 * - Updates the linkedChannels parameter on both affected devices
 * - Can be initiated by either device in the link
 * - For AUX channels, unlinks from the associated device
 * 
 * The message structure mirrors channelLink but indicates the removal of the relationship
 * rather than establishment.
 * 
 * @see Virgil Protocol 2.3.0 - "Linking Channels" section
 * @see ChannelLink for the corresponding link operation
 */
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
        if(!j.contains("messageType"))
            throw std::invalid_argument("ChannelUnlink JSON must contain 'messageType' field");
        
        std::string msgType = j.at("messageType").get<std::string>();
        if(msgType != "channelUnlink")
            throw std::invalid_argument("ChannelUnlink JSON must have messageType='channelUnlink', but received messageType='" + msgType + "'");
        
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("ChannelUnlink JSON must contain 'messageID' field");
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
            throw std::invalid_argument("Non-AUX sendingChannel (type=" + std::to_string(static_cast<int>(sendingChannel.channelType)) + 
                ", index=" + std::to_string(sendingChannel.channelIndex) + ") must have a receivingChannel. Only AUX channels can omit receivingChannel.");
        if (receivingChannel) {
            receivingChannel->AppendJson(j);
        }
        if(responseID)
            j["responseID"] = responseID->to_string();
        return j;
    }


};

/**
 * @brief Virgil Protocol message indicating the end of a response sequence.
 * 
 * The endResponse message is used to signal that the sender has no more responses
 * to provide in the current communication session. This message helps manage
 * request-response patterns and indicates completion of multi-message exchanges.
 * 
 * Usage patterns:
 * - Sent to indicate completion of device discovery exchanges
 * - Marks the end of parameter information responses
 * - Signals completion of status update sequences
 * - Always includes a responseID referencing the original request
 * 
 * According to the protocol specification, endResponse messages should not 
 * have a responseID when used to end a communication session, but must have
 * one when responding to a specific request.
 * 
 * @see Virgil Protocol 2.3.0 - "Communication Session Management" section
 * @note This message type is crucial for proper session management and avoiding timeouts
 */
class EndResponse : public Message {
public:
    // Constructs an EndResponse from a JSON object.
    EndResponse(const nlohmann::json& j, bool outbound) {
        // Double checks that this is an EndResponse message
        if(!j.contains("messageType"))
            throw std::invalid_argument("EndResponse JSON must contain 'messageType' field");
        
        std::string msgType = j.at("messageType").get<std::string>();
        if(msgType != "endResponse")
            throw std::invalid_argument("EndResponse JSON must have messageType='endResponse', but received messageType='" + msgType + "'");
        
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("EndResponse JSON must contain 'messageID' field");
        if(!j.contains("responseID"))
            throw std::invalid_argument("EndResponse JSON must contain 'responseID' field");
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

/**
 * @brief Virgil Protocol message for conveying errors in communication.
 * 
 * The errorResponse message is used to indicate that an error occurred while processing
 * a request. It provides both a standardized error code (errorValue) and a human-readable
 * description (errorString) to help with debugging and user feedback.
 * 
 * Standard error types defined in Virgil Protocol 2.3.0:
 * - UnrecognizedCommand: Unknown message type
 * - ValueOutOfRange: Parameter value outside allowed range
 * - InvalidValueType: Wrong data type for parameter
 * - UnableToChangeValue: Parameter cannot be modified currently
 * - DeviceNotFound: Target device not available
 * - ChannelIndexInvalid: Channel does not exist
 * - ParameterReadOnly: Parameter is read-only or disabled
 * - ParameterUnsupported: Parameter not supported by device
 * - MalformedMessage: Invalid JSON or message structure
 * - Busy: Device cannot process request currently
 * - Timeout: Request timed out
 * - PermissionDenied: Insufficient privileges
 * - InternalError: Device internal error
 * - OutOfResources: Device resource exhaustion
 * - NetworkError: Network communication problem
 * - Custom:Description: Custom error (replace Description with specific details)
 * 
 * @see Virgil Protocol 2.3.0 - "Error Types" section
 * @note Always provide clear, user-friendly error messages in errorString
 */
class ErrorResponse : public Message {
public:
    MessageID responseID; // The ID of the message this is responding to
    std::string errorValue; // The predefined error type
    std::string errorString; // Human-readable error message

    // Constructs an ErrorResponse from a JSON object.
    ErrorResponse(const nlohmann::json& j, bool outbound) {
        // Double checks that this is an ErrorResponse message
        if(!j.contains("messageType"))
            throw std::invalid_argument("ErrorResponse JSON must contain 'messageType' field");
        
        std::string msgType = j.at("messageType").get<std::string>();
        if(msgType != "errorResponse")
            throw std::invalid_argument("ErrorResponse JSON must have messageType='errorResponse', but received messageType='" + msgType + "'");
        
        // Makes sure required fields are present
        if(!j.contains("messageID"))
            throw std::invalid_argument("ErrorResponse JSON must contain 'messageID' field");
        if(!j.contains("responseID"))
            throw std::invalid_argument("ErrorResponse JSON must contain 'responseID' field");
        if(!j.contains("errorValue"))
            throw std::invalid_argument("ErrorResponse JSON must contain 'errorValue' field");
        if(!j.contains("errorString"))
            throw std::invalid_argument("ErrorResponse JSON must contain 'errorString' field");

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

/**
 * @brief Virgil Protocol message for requesting information about a channel.
 * 
 * The infoRequest message is used to request detailed information about a specific
 * channel on a device. This includes all parameters supported by the channel,
 * their current values, constraints, and the list of linked channels.
 * 
 * Channel discovery flow:
 * 1. Send infoRequest specifying channelIndex and channelType
 * 2. Receive infoResponse with complete channel parameter information
 * 3. Use received information to understand channel capabilities
 * 
 * The response will include:
 * - All supported parameters (gain, pad, phantomPower, etc.)
 * - Parameter constraints (min/max values, precision, read-only status)
 * - Current parameter values
 * - LinkedChannels information showing connected devices/channels
 * 
 * This message is typically used during device discovery after establishing
 * a connection and exchanging device-level information.
 * 
 * @see Virgil Protocol 2.3.0 - "Channel Discovery" section
 * @see InfoResponse for the corresponding response message
 * @see DeviceInfoRequest for device-level information requests
 */
class InfoRequest : public Message {
public:
    ChannelID channel; // The channel to request info about
    // Constructs an InfoRequest from a JSON object.
    InfoRequest(const nlohmann::json& j, bool outbound) {
        // Double checks that this is an InfoRequest message
        if(!j.contains("messageType"))
            throw std::invalid_argument("InfoRequest JSON must contain 'messageType' field");
        
        std::string msgType = j.at("messageType").get<std::string>();
        if(msgType != "infoRequest")
            throw std::invalid_argument("InfoRequest JSON must have messageType='infoRequest', but received messageType='" + msgType + "'");
        
        // Makes sure messageID is present
        if(!j.contains("messageID"))
            throw std::invalid_argument("InfoRequest JSON must contain 'messageID' field");

        // Reads messageID and constructs from string
        selfID = MessageID(j.at("messageID").get<std::string>());

        channel = ChannelID(j);

        // Reads responseID if present. This is not required for InfoRequest messages.
        if(j.contains("responseID"))
            responseID = MessageID(j.at("responseID").get<std::string>());

        // Sets outbound status. This must be provided by the caller since the bare message does not include this information.
        isOutbound = outbound;
    }

    /// @brief Constructs an InfoRequest with given parameters.
    /// @param msgId The message ID of this InfoRequest.
    /// @param outbound True if the message is outbound, false if inbound.
    /// @param channelId The channel ID associated with this InfoRequest.
    /// @param respId The response ID for this InfoRequest.
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
        channel.AppendJson(j);
        return j;
    }
};

/**
 * @brief Virgil Protocol message responding to an InfoRequest with channel information.
 * 
 * The infoResponse message provides comprehensive information about a channel in response
 * to an infoRequest. It contains all parameters supported by the channel, their current
 * values, constraints, and linking information.
 * 
 * Content includes:
 * - Channel identification (channelIndex, channelType)
 * - linkedChannels array showing connected devices and their channels
 * - All supported parameters with full specifications:
 *   * Control parameters: gain (required), pad, lowcut, polarity, phantomPower, etc.
 *   * Status parameters: deviceConnected, subDevice (read-only)
 *   * Continuous parameters: audioLevel, rfLevel, batteryLevel (read-only)
 * 
 * Parameter information includes:
 * - Current value
 * - Data type (int, float, bool, string, enum)
 * - Constraints (minValue, maxValue, precision for numeric types)
 * - Units of measurement
 * - Read-only status
 * - Enum values (for enum parameters)
 * 
 * The gain parameter is mandatory for all channels involving a preamp.
 * The linkedChannels parameter is mandatory and starts empty, being populated
 * as channels are linked/unlinked.
 * 
 * @see Virgil Protocol 2.3.0 - "Parameters" and "Channel Discovery" sections
 * @see InfoRequest for the corresponding request message
 * @see Parameter struct for individual parameter structure
 * @see LinkedChannelInfo for linked channel information structure
 */
class InfoResponse : public Message {
    ChannelID channel; // The channel to request info about
    std::vector<LinkedChannelInfo> linkedChannels; // List of linked channels
    std::vector<Parameter> parameters; // List of parameters for the channel

    // Constructs an InfoResponse from a JSON object.
    InfoResponse(const nlohmann::json& j, bool outbound) {
        // Double checks that this is an InfoResponse message
        if(!j.contains("messageType"))
            throw std::invalid_argument("InfoResponse JSON must contain 'messageType' field");
        
        std::string msgType = j.at("messageType").get<std::string>();
        if(msgType != "infoResponse")
            throw std::invalid_argument("InfoResponse JSON must have messageType='infoResponse', but received messageType='" + msgType + "'");
        
        // Makes sure required fields are present
        if(!j.contains("messageID"))
            throw std::invalid_argument("InfoResponse JSON must contain 'messageID' field");
        // Reads responseID if present. This is required for InfoResponse messages.
        if(!j.contains("responseID"))
            throw std::invalid_argument("InfoResponse JSON must contain 'responseID' field");

        // Reads messageID and constructs from string
        selfID = MessageID(j.at("messageID").get<std::string>());
        responseID = MessageID(j.at("responseID").get<std::string>());

        // Read channel identification information
        channel = ChannelID(j);

        // Parse all additional fields in the JSON as either linkedChannels or parameters
        // This follows Virgil protocol where everything except messageType, messageID, responseID,
        // channelIndex, and channelType are treated as channel parameters
        for (const auto& [key, value] : j.items()) {
            // Skip the standard message and channel identification fields
            if (key == "messageType" || key == "messageID" || key == "responseID" || 
                key == "channelIndex" || key == "channelType") {
                continue; // These are handled separately above
            }
            
            // Special handling for linkedChannels - this is a mandatory parameter per Virgil protocol
            // that contains an array of linked device/channel information
            if(key == "linkedChannels"){
                if(!value.is_array())
                    throw std::invalid_argument("Field 'linkedChannels' must be an array, but received type: " + 
                        std::string(value.type_name()));
                for(size_t i = 0; i < value.size(); ++i){
                    const auto& item = value[i];
                    if(!item.is_object())
                        throw std::invalid_argument("linkedChannels[" + std::to_string(i) + "] must be an object, but received type: " + 
                            std::string(item.type_name()));
                    linkedChannels.push_back(LinkedChannelInfo(item));
                }
                continue;
            }
            
            // All other fields are treated as channel parameters (gain, pad, phantomPower, etc.)
            // Each parameter contains value, dataType, readOnly, and constraints
            parameters.push_back(Parameter(key, value));
        }
        // Sets outbound status. This must be provided by the caller since the bare message does not include this information.
        isOutbound = outbound;
    }

    /// @brief Constructs an InfoResponse with given parameters.
    /// @param msgId The message ID of this InfoResponse.
    /// @param outbound True if the message is outbound, false if inbound.
    /// @param channelId The channel ID associated with this InfoResponse.
    /// @param linkedChans The linked channels for this InfoResponse.
    /// @param params The parameters for this InfoResponse.
    /// @param respId The response ID for this InfoResponse.
    InfoResponse(const MessageID& msgId, bool outbound, const ChannelID& channelId, const std::vector<LinkedChannelInfo>& linkedChans, const std::vector<Parameter>& params, MessageID respId) {
        responseID = respId;
        selfID = msgId;
        isOutbound = outbound;
        channel = channelId;
        linkedChannels = linkedChans;
        parameters = params;
    }
    // Converts the InfoRequest to a JSON object for sending.
    nlohmann::json to_json() const override {
        //Validates that responseID is present
        if(!responseID)
            throw std::invalid_argument("InfoResponse must have a responseID to identify which request it responds to");
        nlohmann::json j;
        //Sets basic fields
        j["messageType"] = "infoRequest";

        //Generates messageID if missing
        if(selfID)
            j["messageID"] = selfID.to_string();
        else 
            j["messageID"] = MessageID::GenerateNew().to_string();

        //Sets responseID
        j["responseID"] = responseID->to_string();

        //Appends all linked channels and parameters
        j["linkedChannels"] = nlohmann::json::array();
        for (const auto& linkedChan : linkedChannels) {
            j["linkedChannels"].push_back(linkedChan.to_json());
        }
        //Appends all parameters
        for (const auto& param : parameters) {
            param.append_json(j);
        }
        //Adds channel id
        channel.AppendJson(j);

        return j;
    }
};

/**
 * @brief A struct representing a Virgil Protocol parameter with validation and JSON serialization.
 * 
 * Parameters are the core data structure for controlling and monitoring audio device settings
 * in the Virgil Protocol 2.3.0. Each parameter contains a current value, data type information,
 * constraints, and metadata required for proper validation and UI generation.
 * 
 * Supported parameter types and their requirements:
 * 
 * **Control Parameters** (user-controllable):
 * - gain (required): Analog gain, independent of pad. Units: dB. Type: int/float
 * - pad: Input attenuator control. Type: bool
 * - padLevel: Pad attenuation amount. Units: dB. Type: int (usually negative)
 * - lowcut: High-pass filter frequency. Units: Hz. Type: int/float  
 * - lowcutEnable: Enable/disable high-pass filter. Type: bool
 * - polarity: Signal polarity (phase invert). Type: bool
 * - phantomPower: Phantom power control. Type: bool
 * - rfEnable: RF transmitter/receiver control. Type: bool
 * - transmitPower: Transmitter power level. Type: enum/int/float. Units: %
 * - squelch: Squelch threshold. Type: int/float. Units: dB or %
 * 
 * **Status Parameters** (read-only):
 * - deviceConnected: Wireless device connection status. Type: bool
 * - subDevice: Type of connected device. Type: string (handheld, beltpack, etc.)
 * 
 * **Continuous Parameters** (real-time monitoring, read-only):
 * - audioLevel: Audio signal level. Type: float. Units: dBFS
 * - rfLevel: RF signal strength. Type: int/float. Units: dB or %
 * - batteryLevel: Battery level. Type: int. Units: %
 * 
 * **Special Parameters**:
 * - linkedChannels: Mandatory array of linked channel information
 * 
 * Data type validation follows the formula for numeric parameters:
 * `isValid = (value - minValue) % precision == 0 && value >= minValue && value <= maxValue`
 * 
 * @see Virgil Protocol 2.3.0 - "Parameters" section for complete specification
 * @note Non-readonly numeric parameters must specify minValue, maxValue, and precision
 */
struct Parameter {
    std::string name; // Parameter name
    std::string dataType; // "number", "bool", "string", or "enum"
    std::optional<std::string> unit; // Unit of measurement. Shorthand like "dB" or "Hz"
    std::variant<int,float,bool,std::string> value; // Current value
    std::optional<std::variant<int,float>> minValue; // Minimum value for number types
    std::optional<std::variant<int,float>> maxValue; // Maximum value for number types
    std::optional<std::variant<int,float>> precision; // Precision for number types
    bool readOnly; // True if parameter is read-only

    /// @brief Constructs a Parameter from a JSON object.
    /// @param j The JSON object to parse. This should not contain the name.
    Parameter(const std::string& paramName, const nlohmann::json& j)
    {
        // Validates presence of required fields
        if(!j.contains("dataType"))
            throw std::invalid_argument("Parameter '" + paramName + "' JSON must contain 'dataType' field");
        if(!j.contains("value"))
            throw std::invalid_argument("Parameter '" + paramName + "' JSON must contain 'value' field");
        if(!j.contains("readOnly"))
            throw std::invalid_argument("Parameter '" + paramName + "' JSON must contain 'readOnly' field");
        
        bool isReadOnly = j.at("readOnly").get<bool>();
        std::string dataTypeStr = j.at("dataType").get<std::string>();

        // Parses based on dataType
        if(dataTypeStr == "string") {
            std::string value = j.at("value").get<std::string>();
            *this = Parameter(paramName, value, isReadOnly);
        }
        else if(dataTypeStr == "bool") {
            bool value = j.at("value").get<bool>();
            *this = Parameter(paramName, value, isReadOnly);
        }
        else if(dataTypeStr == "enum") {
            // For enum, we need to reconstruct the VirgilEnum from the JSON
            std::string value = j.at("value").get<std::string>();
            if(!j.contains("enumValues"))
                throw std::invalid_argument("Enum parameter '" + paramName + "' JSON must contain 'enumValues' field");
            std::vector<std::string> enumValues = j.at("enumValues").get<std::vector<std::string>>();
            VirgilEnum enumValue(value, enumValues);
            *this = Parameter(paramName, enumValue, isReadOnly);
        }
        else if(dataTypeStr == "int") {
            if(!j.contains("unit"))
                throw std::invalid_argument("Integer parameter '" + paramName + "' JSON must contain 'unit' field");
            std::string unitStr = j.at("unit").get<std::string>();
            int value = j.at("value").get<int>();
                std::optional<int> minVal, maxVal, prec;
                if(j.contains("minValue"))
                    minVal = j.at("minValue").get<int>();
                if(j.contains("maxValue"))
                    maxVal = j.at("maxValue").get<int>();
                if(j.contains("precision"))
                    prec = j.at("precision").get<int>();
                *this = Parameter(paramName, value, isReadOnly, unitStr, minVal, maxVal, prec);
        }
        else if(dataTypeStr == "float")
        {
            if(!j.contains("unit"))
                throw std::invalid_argument("Float parameter '" + paramName + "' JSON must contain 'unit' field");
            std::string unitStr = j.at("unit").get<std::string>();

            float value = j.at("value").get<float>();
            std::optional<float> minVal, maxVal, prec;
            if(j.contains("minValue"))
                minVal = j.at("minValue").get<float>();
            if(j.contains("maxValue"))
                maxVal = j.at("maxValue").get<float>();
            if(j.contains("precision"))
                prec = j.at("precision").get<float>();
            *this = Parameter(paramName, value, isReadOnly, unitStr, minVal, maxVal, prec);
        }
        else {
            throw std::invalid_argument("Parameter '" + paramName + "' has unknown dataType: '" + dataTypeStr + 
                "'. Supported types: 'string', 'bool', 'enum', 'int', 'float'");
        }
    }

    // Constructor for string parameters
    Parameter(const std::string& paramName, const std::string& paramValue, bool isReadOnly)
    {
        if(paramName.empty())
            throw std::invalid_argument("Parameter name cannot be empty when creating string parameter");
        name = paramName;
        dataType = "string";
        value = paramValue;
        readOnly = isReadOnly;
    }

    // Constructor for VirgilEnum parameters. These are just strings with a predefined set of valid values.
    Parameter(const std::string& paramName, const VirgilEnum& paramValue, bool isReadOnly)
    {
        if(paramName.empty())
            throw std::invalid_argument("Parameter name cannot be empty when creating enum parameter '" + paramName + "'");
        if(!paramValue)
            throw std::invalid_argument("Invalid enum value for parameter '" + paramName + "'. Enum value '" + 
                paramValue.value + "' is not in the allowed values list.");
        name = paramName;
        dataType = "enum";
        value = paramValue;
        readOnly = isReadOnly;
    }

    // Constructor for int parameters
    Parameter(const std::string& paramName, int paramValue, bool isReadOnly, const std::string& unitStr, std::optional<int> minVal, std::optional<int> maxVal, std::optional<int> prec)
    {
        if(paramName.empty())
            throw std::invalid_argument("Parameter name cannot be empty when creating integer parameter");
        if(unitStr.empty())
            throw std::invalid_argument("Unit cannot be empty for integer parameter '" + paramName + "'");
        if((minVal && maxVal) && (*minVal > *maxVal))
            throw std::invalid_argument("Parameter '" + paramName + "': minValue (" + std::to_string(*minVal) + 
                ") cannot be greater than maxValue (" + std::to_string(*maxVal) + ")");
        if(prec && *prec <= 0)
            throw std::invalid_argument("Parameter '" + paramName + "': precision (" + std::to_string(*prec) + 
                ") must be greater than 0");
        if(!isReadOnly && (!minVal || !maxVal || !prec))
            throw std::invalid_argument("Non-readonly integer parameter '" + paramName + 
                "' must have minValue, maxValue, and precision specified");
        if(!isReadOnly && minVal && maxVal && prec) {
            if((paramValue - *minVal) % *prec != 0 || paramValue < *minVal || paramValue > *maxVal)
                throw std::invalid_argument("Parameter '" + paramName + "': initial value (" + std::to_string(paramValue) + 
                    ") is not valid. Must be between " + std::to_string(*minVal) + " and " + std::to_string(*maxVal) + 
                    " in steps of " + std::to_string(*prec));
        }
        name = paramName;
        dataType = "number";
        value = paramValue;
        unit = unitStr;
        if(minValue)
            minValue = minVal;
        if(maxValue)
            maxValue = maxVal;
        if(precision)
            precision = prec;
        readOnly = isReadOnly;
    }

    // Constructor for float parameters
    Parameter(const std::string& paramName, float paramValue, bool isReadOnly, const std::string& unitStr, std::optional<float> minVal, std::optional<float> maxVal, std::optional<float> prec)
    {
        if(paramName.empty())
            throw std::invalid_argument("Parameter name cannot be empty when creating float parameter");
        if(unitStr.empty())
            throw std::invalid_argument("Unit cannot be empty for float parameter '" + paramName + "'");
        if((minVal && maxVal) && (*minVal > *maxVal))
            throw std::invalid_argument("Parameter '" + paramName + "': minValue (" + std::to_string(*minVal) + 
                ") cannot be greater than maxValue (" + std::to_string(*maxVal) + ")");
        if(prec && *prec <= 0)
            throw std::invalid_argument("Parameter '" + paramName + "': precision (" + std::to_string(*prec) + 
                ") must be greater than 0");
        if(!isReadOnly && (!minVal || !maxVal || !prec))
            throw std::invalid_argument("Non-readonly float parameter '" + paramName + 
                "' must have minValue, maxValue, and precision specified");
        name = paramName;
        dataType = "number";
        value = paramValue;
        unit = unitStr;
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
            throw std::invalid_argument("Parameter name cannot be empty when creating boolean parameter");
        name = paramName;
        dataType = "bool";
        value = paramValue;
        readOnly = isReadOnly;
    }

    // Converts the Parameter to a JSON object for sending. This does not contain the name field.
    nlohmann::json to_json() const {

        if(name.empty())
            throw std::invalid_argument("Parameter name cannot be empty when converting to JSON. Parameter has dataType='" + 
                dataType + "' but missing name");
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
    // This ensures the parameter conforms to Virgil Protocol 2.3.0 parameter requirements
    operator bool() const {
        // Basic validation: parameter must have a name
        if(name.empty())
            return false;

        // Validate enum parameters according to Virgil protocol requirements
        if(dataType == "enum") {
            // Enum value must be stored as VirgilEnum variant
            if(!std::holds_alternative<VirgilEnum>(value))
                return false;
            // The enum itself must be valid (value in enumValues list)
            VirgilEnum enumVal = std::get<VirgilEnum>(value);
            if(!enumVal)
                return false;
        }
        // Validate numeric parameters (int/float) according to Virgil protocol requirements
        else if(dataType == "number")
        {
            // Numeric value must be stored as int or double variant
            if(!std::holds_alternative<int>(value) && !std::holds_alternative<double>(value))
                return false;

            // For non-readonly numeric parameters, Virgil protocol requires minValue, maxValue, and precision
            // This allows proper validation and UI generation
            if(!readOnly) {
                // All three constraint fields are mandatory for editable numeric parameters
                if(!minValue || !maxValue || !precision)
                    return false;

                // Type consistency check: if value is int, constraints must also be int
                // This ensures proper type handling in JSON serialization/deserialization
                if(std::holds_alternative<int>(value)) {
                    if(!std::holds_alternative<int>(*minValue) || 
                       !std::holds_alternative<int>(*maxValue) || 
                       !std::holds_alternative<int>(*precision))
                        return false;
                } else if(std::holds_alternative<double>(value)) {
                    // Similarly for float/double values, all constraints must be float/double
                    if(!std::holds_alternative<double>(*minValue) || 
                       !std::holds_alternative<double>(*maxValue) || 
                       !std::holds_alternative<double>(*precision))
                        return false;
                }
            }
        }
        // Validate boolean parameters (simple true/false values)
        else if (dataType == "bool") {
            // Boolean value must be stored as bool variant
            if(!std::holds_alternative<bool>(value))
                return false;
        }
        // Validate string parameters (device names, model info, etc.)
        else if (dataType == "string") {
            // String value must be stored as string variant
            if(!std::holds_alternative<std::string>(value))
                return false;
        }
        else
            return false; // Unknown/unsupported dataType
        return true; // All validations passed
    }
};

/**
 * @brief A struct representing a string enumeration with validation for Virgil Protocol parameters.
 * 
 * VirgilEnum is used for parameters that accept only specific predefined string values.
 * This is commonly used for device settings that have discrete options rather than
 * continuous numeric ranges.
 * 
 * Common enum parameter examples in Virgil Protocol:
 * - transmitPower: ["low", "medium", "high"] for IEM systems
 * - subDevice: ["handheld", "beltpack", "gooseneck", "iem", "xlr", "trs", "disconnected", "other"]
 * - Custom device-specific settings with predefined options
 * 
 * The enum maintains both the current value and the complete list of valid values,
 * enabling proper validation and UI generation (dropdown menus, radio buttons, etc.).
 * 
 * Validation ensures that:
 * - At least one valid value exists in enumValues
 * - The current value is present in the enumValues list
 * - The enum can be safely serialized to/from JSON
 * 
 * @see Virgil Protocol 2.3.0 - "Parameter Structure" for enum parameter requirements
 * @note An empty or invalid enum (where value is not in enumValues) will fail validation
 */
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
    // According to Virgil protocol, enums must have at least one valid value and the current value must be in the list
    operator bool() const {
        // Must have at least one valid enum value defined
        if(enumValues.size() < 1)
            return false;

        // Current value must be present in the enumValues list
        // This ensures the enum is in a valid state per Virgil protocol requirements
        for(const auto& v : enumValues)
        {
            if(v == value)
                // Exit early when a match is found - enum is valid
                return true;
        }
        return false; // Current value not found in valid values list
    }

    bool operator==(const VirgilEnum& other) const {
        if(!*this || !other)
            throw std::invalid_argument("Cannot compare invalid VirgilEnums. Left enum valid=" + std::to_string(static_cast<bool>(*this)) + 
                " (value='" + value + "'), Right enum valid=" + std::to_string(static_cast<bool>(other)) + 
                " (value='" + other.value + "')");
        return value == other.value && enumValues == other.enumValues;
    }

    bool operator!=(const VirgilEnum& other) const {
        return !(*this == other);
    }

};

/**
 * @brief A struct representing a unique channel identifier in the Virgil Protocol 2.3.0.
 * 
 * ChannelID combines a channel index (0-based) with a channel type to uniquely identify
 * any channel within the Virgil audio network. This identifier is used throughout the
 * protocol for routing audio, managing parameters, and establishing device connections.
 * 
 * Channel Type Meanings:
 * - TX (0): Dante transmitting channels that send audio data to other devices
 * - RX (1): Dante receiving channels that receive audio data from other devices  
 * - AUX (2): Auxiliary channels for non-Dante device control (e.g., wall panels, dials)
 * 
 * Usage Patterns:
 * - Audio routing: TX channels link to RX channels to establish audio flow
 * - Parameter control: Each channel can have device-specific parameters (gain, phantom power, etc.)
 * - Device discovery: Enumerate all channels during connection establishment
 * - Status monitoring: Subscribe to parameter changes on specific channels
 * 
 * Linking Rules:
 * - TX ↔ RX: Bidirectional linking for audio flow (either side can initiate)
 * - AUX → Device: AUX channels link to devices, not other channels
 * - Channel indices start at 0 and must correspond to actual device capabilities
 * 
 * JSON Serialization:
 * - Standard fields: "channelIndex" and "channelType" 
 * - Custom fields: Allow flexible field naming for different message contexts
 * 
 * @see Virgil Protocol 2.3.0 - "Channel Types" and "Linking Channels" sections
 * @note Every Dante channel must have a corresponding Virgil channel with appropriate type
 */
struct ChannelID {
    uint16_t channelIndex; // Index of the channel
    LinkType channelType; // Type of the channel, e.g. "aux", "tx", "rx", "device"

    ChannelID() : channelIndex(0), channelType(LinkType::tx) {}

    // Constructor for when both channel index and type are provided
    ChannelID(const int index, const LinkType type)
    {
        if(index < 0)
            throw std::invalid_argument("Channel index (" + std::to_string(index) + ") cannot be negative. Valid range is 0 and above.");
        channelType = type;
        channelIndex = index;
    }

    // Scans a JSON object for channelIndex and channelType, and constructs a ChannelID.
    ChannelID(const nlohmann::json& j) : ChannelID(j, "channelIndex", "channelType") {}

    // Scans a JSON object for channelIndex and channelType with custom field names, and constructs a ChannelID.
    ChannelID(const nlohmann::json& j, const std::string& channelindexName, const std::string& channelTypeName) {
        if(!j.contains(channelTypeName))
            throw std::invalid_argument("ChannelID JSON must contain field '" + channelTypeName + "'. Available fields: " + 
                [&j]() {
                    std::string fields = "";
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        if (!fields.empty()) fields += ", ";
                        fields += "'" + it.key() + "'";
                    }
                    return fields.empty() ? "(none)" : fields;
                }());
        
        if(!j.at(channelTypeName).is_number_unsigned())
            throw std::invalid_argument("Field '" + channelTypeName + "' must be an unsigned integer, but received type: " + 
                std::string(j.at(channelTypeName).type_name()) + " with value: " + j.at(channelTypeName).dump());
            
        channelType = static_cast<LinkType>(j.at(channelTypeName).get<uint8_t>());

        if(!j.contains(channelindexName))
            throw std::invalid_argument("ChannelID JSON must contain field '" + channelindexName + "'. Available fields: " + 
                [&j]() {
                    std::string fields = "";
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        if (!fields.empty()) fields += ", ";
                        fields += "'" + it.key() + "'";
                    }
                    return fields.empty() ? "(none)" : fields;
                }());
        
        if(!j.at(channelindexName).is_number_unsigned())
            throw std::invalid_argument("Field '" + channelindexName + "' must be an unsigned integer, but received type: " + 
                std::string(j.at(channelindexName).type_name()) + " with value: " + j.at(channelindexName).dump());

        channelIndex = j.at(channelindexName).get<uint16_t>();
    }

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
        j[channelindexName] = channelIndex;
        j[channelTypeName] = channelType;
        return j;
    }

    // Appends the ChannelID fields to an existing JSON object with custom field names.
    // @param j The JSON object to append to. It does not clear existing fields, but will overwrite the fields if they already exist.
    // @param channelindexName The field name to store the channel index in.
    // @param channelTypeName The field name to store the channel type in.
    void AppendJson(nlohmann::json& j, const std::string& channelindexName, const std::string& channelTypeName) const {
        j[channelindexName] = channelIndex;
        j[channelTypeName] = channelType;
    }

    // Appends the ChannelID fields to an existing JSON object with field names of "channelIndex" and "channelType".
    // @param j The JSON object to append to. It does not clear existing fields, but will overwrite the fields if they already exist.
    void AppendJson(nlohmann::json& j) const {
        AppendJson(j, "channelIndex", "channelType");
    }


    // Checks if the channel is an auxiliary channel.
    bool IsAux() const {
        return channelType == LinkType::aux;
    }

    // Checks if the channel is a transmit channel.
    bool IsTx() const {
        return channelType == LinkType::tx;
    }

    // Checks if the channel is a receive channel.
    bool IsRx() const {
        return channelType == LinkType::rx;
    }

    // Gets the index of the channel.
    // @return The channel index.
    // @throws std::runtime_error if the channel is device-level (no index) or if the index is not set.
    int GetIndex() const {
        return channelIndex;
    }

    bool operator==(const ChannelID& other) const {
        return channelIndex == other.channelIndex && channelType == other.channelType;
    }
    bool operator!=(const ChannelID& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Enumeration representing the three types of channels in the Virgil Protocol 2.3.0.
 * 
 * The Virgil protocol defines three distinct channel types that correspond to different
 * audio routing and device control scenarios:
 * 
 * - TX channels (0): Dante transmitting channels that send audio data
 * - RX channels (1): Dante receiving channels that receive audio data  
 * - AUX channels (2): Auxiliary non-Dante channels for accessory control
 * 
 * Channel linking rules:
 * - TX channels can only link to RX channels and vice versa
 * - AUX channels link to devices rather than other channels
 * - Every Dante channel must have a corresponding Virgil channel
 * 
 * For example, a device with 12 Dante transmitting channels and 8 Dante receiving
 * channels would advertise 12 TX channels and 8 RX channels in Virgil.
 * 
 * @see Virgil Protocol 2.3.0 - "Channel Types" section
 * @note Channel indices start at 0 for all types
 */
enum class LinkType : uint8_t {
    tx = 0,   ///< Transmit channels - Dante transmitting channels that send audio data
    rx = 1,   ///< Receive channels - Dante receiving channels that receive audio data  
    aux = 2   ///< Auxiliary channels - Non-Dante accessory channels (e.g., in-wall dials)
};

/**
 * @brief A struct representing linked channel information for the mandatory linkedChannels parameter.
 * 
 * LinkedChannelInfo describes a connection between the current channel and a channel on another
 * device in the Virgil audio network. This information is stored in the mandatory "linkedChannels"
 * parameter and represents the actual audio flow established via Dante networking.
 * 
 * The structure varies based on channel type:
 * 
 * **For TX/RX Channels** (audio flow):
 * ```json
 * {
 *   "deviceName": "ConnectedDeviceName",
 *   "channelIndex": 0,
 *   "channelType": 1
 * }
 * ```
 * 
 * **For AUX Channels** (device control):
 * ```json
 * {
 *   "deviceName": "ConnectedDeviceName"
 *   // No channelIndex/channelType - AUX links to device, not specific channel
 * }
 * ```
 * 
 * Protocol Requirements:
 * - linkedChannels array starts empty and is populated as connections are made
 * - TX channels can link to multiple RX channels (one-to-many audio distribution)
 * - RX channels typically link to one TX channel (audio source)
 * - AUX channels link to devices for control purposes
 * - If channels are subscribed in Dante, they must be linked in Virgil
 * 
 * This information enables:
 * - Audio routing visualization and management
 * - Automatic parameter subscription management  
 * - Network topology discovery and validation
 * - Conflict detection and resolution
 * 
 * @see Virgil Protocol 2.3.0 - "Parameters" section for linkedChannels specification
 * @see ChannelID for channel identification details
 */
struct LinkedChannelInfo {
    std::string deviceName;
    ChannelID channel;

    // Default constructor. Will be considered invalid until properly initialized.
    LinkedChannelInfo() = default;

    // Constructor with device name and channel
    LinkedChannelInfo(const std::string& devName, const ChannelID& chan) : deviceName(devName), channel(chan) {
        if(devName.empty())
            throw std::invalid_argument("LinkedChannelInfo device name cannot be empty. Channel info: type=" + 
                std::to_string(static_cast<int>(chan.channelType)) + ", index=" + std::to_string(chan.channelIndex));
    }

    // Constructor from JSON
    LinkedChannelInfo(const nlohmann::json& j) {
        // Checks for deviceName field
        if(!j.contains("deviceName"))
            throw std::invalid_argument("LinkedChannelInfo JSON must contain 'deviceName' field. Available fields: " + 
                [&j]() {
                    std::string fields = "";
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        if (!fields.empty()) fields += ", ";
                        fields += "'" + it.key() + "'";
                    }
                    return fields.empty() ? "(none)" : fields;
                }());
        
        std::string devName = j.at("deviceName").get<std::string>();
        if(devName.empty())
            throw std::invalid_argument("LinkedChannelInfo 'deviceName' field cannot be empty");
        deviceName = devName;

        // Creates ChannelID from JSON
        channel = ChannelID(j);

        // Checks validity
        if(!*this)
            throw std::invalid_argument("Invalid LinkedChannelInfo: deviceName='" + deviceName + 
                "', channelType=" + std::to_string(static_cast<int>(channel.channelType)) + 
                ", channelIndex=" + std::to_string(channel.channelIndex));
    }

    nlohmann::json to_json() const {
        if(!*this)
            throw std::invalid_argument("Cannot convert invalid LinkedChannelInfo to JSON. DeviceName='" + deviceName + 
                "', channelType=" + std::to_string(static_cast<int>(channel.channelType)) + 
                ", channelIndex=" + std::to_string(channel.channelIndex));
        nlohmann::json j;
        j["deviceName"] = deviceName;
        channel.AppendJson(j);
        return j;
    }

    operator bool() const {
        return !deviceName.empty();
    }
};

/**
 * @brief A struct representing a unique message identifier for Virgil Protocol 2.3.0 messages.
 * 
 * MessageID provides unique identification for every message in the Virgil protocol, enabling
 * proper request-response correlation, logging, and debugging. The ID combines timestamp 
 * information with a sequence counter to ensure uniqueness even under high message rates.
 * 
 * **Format**: 12-digit string "HHMMSSmmm###"
 * - **HH**: Hour in 24-hour format (00-23)
 * - **MM**: Minute (00-59)
 * - **SS**: Second (00-59)  
 * - **mmm**: Millisecond (000-999)
 * - **###**: Message index within the same millisecond (000-999)
 * 
 * **Example**: "143052847000" = 14:30:52.847 (2:30:52.847 PM), message #0 in that millisecond
 * 
 * **Usage Patterns**:
 * - Every message must have a unique messageID
 * - Response messages include responseID referencing the original request's messageID
 * - endResponse messages should NOT include responseID when ending sessions
 * - statusUpdate messages include responseID only when responding to specific requests
 * 
 * **Implementation Notes**:
 * - Timestamps use local device time (devices may not be synchronized)
 * - Not recommended for precise timing - use only for logging and correlation
 * - Auto-generation ensures uniqueness within a single device
 * - Particularly important for AUX devices not connected via Dante
 * 
 * **Thread Safety**: 
 * - GenerateNew() uses static counters and should be called from a single thread
 * - Multiple simultaneous calls may need synchronization depending on implementation
 * 
 * @see Virgil Protocol 2.3.0 - "Message ID" section for complete specification
 * @note Do not use for anything beyond logging - device timestamps may not be synchronized
 */
struct MessageID {
    static std::chrono::system_clock::time_point timeLastSent; ///< Timestamp of the last generated message (for uniqueness)
    static uint16_t currentMessageIndex; ///< Counter for messages within the same millisecond

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
    std::chrono::system_clock::time_point timeSent{}; ///< Absolute timestamp when message was created
    // Index of the message within the same millisecond (0-999).
    uint16_t messageIndex = 0; ///< Sequence number for messages sent in the same millisecond

    // Default constructor
    MessageID() = default; 
 
    /// @brief Constructor from format provided in virgil messages
    /// @param id A 12-digit string in the format HHMMSSmmm### where HH is hours, MM is minutes, SS is seconds, mmm is milliseconds, and ### is the message index.
    MessageID(const std::string& id) {
        if (id.length() != 12)
            throw std::invalid_argument("MessageID string must be exactly 12 digits, but received '" + id + 
                "' which has " + std::to_string(id.length()) + " characters. Expected format: HHMMSSmmm###");

        // Validate that all characters are digits
        for (size_t i = 0; i < id.length(); ++i) {
            if (!std::isdigit(id[i])) {
                throw std::invalid_argument("MessageID string '" + id + "' contains non-digit character '" + 
                    id[i] + "' at position " + std::to_string(i) + ". All characters must be digits 0-9.");
            }
        }

        // Extract message index from last 3 digits (### portion of HHMMSSmmm###)
        messageIndex = static_cast<int>(std::stoi(id.substr(9, 3)));

        // Parse timestamp from first 9 digits (HHMMSSmmm portion)
        // Extract: HH (hours), MM (minutes), SS (seconds), mmm (milliseconds)
        std::chrono::milliseconds total_ms = std::chrono::hours(std::stoi(id.substr(0, 2)))      // HH
                  + std::chrono::minutes(std::stoi(id.substr(2, 2)))    // MM
                  + std::chrono::seconds(std::stoi(id.substr(4, 2)))    // SS
                  + std::chrono::milliseconds(std::stoi(id.substr(6, 3))); // mmm
        
        // Convert relative time (since midnight) to absolute time_point
        // The Virgil protocol uses time since midnight of the current day
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_time);
        
        // Calculate midnight of today as our reference point
        std::chrono::system_clock::time_point midnight = std::chrono::system_clock::from_time_t(std::mktime(now_tm));
        
        // Final timestamp = midnight + parsed time offset
        timeSent = midnight + total_ms;
    }

    /// Convert to string to be used in virgil messages
    /// @return HHMMSSmmm### format
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
