#pragma once

#include "cat_osint/net/http_client.hpp"
#include "cat_osint/core/engine.hpp"
#include "cat_osint/connectors/json_api_connector.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cat_osint {

class LocalApiServer {
public:
    explicit LocalApiServer(std::shared_ptr<HttpClient> http);
    bool run(unsigned short port);

private:
    std::shared_ptr<HttpClient> http_;
    Engine engine_;
    mutable std::mutex state_mutex_;
    std::vector<JsonApiConnectorConfig> sources_;
    std::vector<std::chrono::seconds> source_intervals_;
    std::vector<Record> latest_records_;
    std::vector<SourceStatus> latest_source_status_;
    std::string latest_analysis_;
};

} // namespace cat_osint
