#ifndef CUSTOMER_ORDER_MESSAGE_HPP
#define CUSTOMER_ORDER_MESSAGE_HPP

#include "message.hpp"
#include "messagetype.hpp"
#include "../order/order.hpp"

/** Message representing a customer order that should be sent to trader agents (not to the exchange).*/
class CustomerOrderMessage : public Message
{
public:
    CustomerOrderMessage() : Message(MessageType::CUSTOMER_ORDER) {}

    int client_order_id;
    std::string ticker;
    Order::Side side;   // BID or ASK
    int quantity;
    double price;
    double priv_value;  // If needed (e.g. for valuation)

private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & boost::serialization::base_object<Message>(*this);
        ar & client_order_id;
        ar & ticker;
        ar & side;
        ar & quantity;
        ar & price;
        ar & priv_value;
    }
};

typedef std::shared_ptr<CustomerOrderMessage> CustomerOrderMessagePtr;

#endif
