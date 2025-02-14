#ifndef PROFIT_SNAPSHOT_HPP
#define PROFIT_SNAPSHOT_HPP

#include <string>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/string.hpp>
#include "../utilities/csvprintable.hpp"

/** The Profit Snapshot Data **/
class ProfitSnapshot : public CSVPrintable, std::enable_shared_from_this<ProfitSnapshot> {
    public:
        ProfitSnapshot() = default;

        ProfitSnapshot(int agent_id, std::string agent_name, double profit)
            : agent_id(agent_id), agent_name(agent_name), profit(profit) {}

        int agent_id;
        std::string agent_name;
        double profit;

        std::string describeCSVHeaders() const override
        {
            return "agent_id, agent_name, profit"; // CSV headers for the Profit Snapshot
        }

        std::string toCSV() const override
        {
            return std::to_string(agent_id) + ", " + agent_name + ", " + std::to_string(profit);
        }

    private:

        friend std::ostream& operator<<(std::ostream& os, const ProfitSnapshot& data)
        {
            os << "Profit Snapshot:\n" 
            << "AGENT ID: " << data.agent_id << "\n"
            << "AGENT NAME: " << data.agent_name << "\n"
            << "PROFIT: " << data.profit << "\n";
            return os;
        }

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & agent_id;
            ar & agent_name;
            ar & profit;
        }
};

typedef std::shared_ptr<ProfitSnapshot> ProfitSnapshotPtr;

#endif