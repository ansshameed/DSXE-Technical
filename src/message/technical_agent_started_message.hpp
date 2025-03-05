#ifndef TECHNICAL_AGENT_STARTED_MESSAGE_HPP
#define TECHNICAL_AGENT_STARTED_MESSAGE_HPP

#include "message.hpp"
#include "messagetype.hpp"


class TechnicalAgentStartedMessage : public Message {
public:
    TechnicalAgentStartedMessage() {
        type = MessageType::TECHNICAL_AGENT_STARTED;
    }
    
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
        ar & boost::serialization::base_object<Message>(*this);
    }
};
    
typedef std::shared_ptr<TechnicalAgentStartedMessage> TechnicalAgentStartedMessagePtr;
    
#endif 