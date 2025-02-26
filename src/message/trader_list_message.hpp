#ifndef TRADER_LIST_MESSAGE_HPP
#define TRADER_LIST_MESSAGE_HPP

#include "message.hpp"
#include <vector>
#include <string>

// Message sent by Orchestrator to Order Injector with trader addresses
class TraderListMessage : public Message {
public:
    std::vector<std::string> trader_addresses; 

    TraderListMessage() {
        type = MessageType::TRADER_LIST_RESPONSE;
    }

    template<class Archive>
    void serialize(Archive & ar, const unsigned int version) {
        ar & boost::serialization::base_object<Message>(*this);
        ar & trader_addresses;
    }
};
typedef std::shared_ptr<TraderListMessage> TraderListMessagePtr;

#endif
