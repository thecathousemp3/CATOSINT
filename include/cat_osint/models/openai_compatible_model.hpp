#pragma once

#include "cat_osint/core/engine.hpp"
#include "cat_osint/net/http_client.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace cat_osint {

struct OpenAiCompatibleModelConfig {
    std::string endpoint;
    std::string model;
    std::string api_key;
    std::size_t max_output_tokens{700};
    std::size_t max_request_bytes{512 * 1024};
    std::chrono::milliseconds timeout{120000};
};

class OpenAiCompatibleModel final : public LocalModel {
public:
    OpenAiCompatibleModel(
        OpenAiCompatibleModelConfig config,
        std::shared_ptr<HttpClient> http);

    std::string analyze(const std::vector<Record>& records) override;

private:
    OpenAiCompatibleModelConfig config_;
    std::shared_ptr<HttpClient> http_;
};

} // namespace cat_osint
