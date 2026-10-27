#pragma once

#include "cat_osint/net/http_client.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cat_osint {

struct DetectedModel {
    std::string provider;
    std::string model_id;
    std::string completion_endpoint;
};

struct ModelDiscoveryResult {
    std::vector<DetectedModel> models;
    std::vector<std::string> diagnostics;
};

class LocalModelDiscovery {
public:
    explicit LocalModelDiscovery(std::shared_ptr<HttpClient> http);
    ModelDiscoveryResult scan(const std::string& openai_compatible_base_url = {}) const;

private:
    std::shared_ptr<HttpClient> http_;
};

} // namespace cat_osint
