#ifndef PROFIT_MESSAGE_HPP
#define PROFIT_MESSAGE_HPP

#include "message.hpp"
#include <boost/serialization/export.hpp>

class ProfitMessage : public Message
{
public:
    int agent_id;
    std::string agent_name; 
    double profit;

    ProfitMessage() : Message(MessageType::PROFIT) {}

    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & boost::serialization::base_object<Message>(*this);
        ar & agent_id;
        ar & agent_name;
        ar & profit;
    }
};

typedef std::shared_ptr<ProfitMessage> ProfitMessagePtr;

#endif