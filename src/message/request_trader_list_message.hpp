#ifndef REQUEST_TRADER_LIST_MESSAGE_HPP
#define REQUEST_TRADER_LIST_MESSAGE_HPP

#include "message.hpp"

// Message sent by Order Injector to request trader addresses
class RequestTraderListMessage : public Message {
public:
    RequestTraderListMessage() {
        type = MessageType::REQUEST_TRADER_LIST;
    }
};
typedef std::shared_ptr<RequestTraderListMessage> RequestTraderListMessagePtr;

#endif