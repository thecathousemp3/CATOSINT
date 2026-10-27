#pragma once

#include "cat_osint/core/engine.hpp"
#include "cat_osint/net/http_client.hpp"

#include <map>
#include <memory>
#include <string>

namespace cat_osint {

struct JsonApiConnectorConfig {
    std::string name;
    std::string url;
    std::string records_path;
    std::string id_path{"id"};
    std::string title_path{"title"};
    std::string content_path{"body"};
    std::string source_url_path;
    std::map<std::string, std::string> headers;
    std::size_t max_response_bytes{8 * 1024 * 1024};
};

class JsonApiConnector final : public PublicApiConnector {
public:
    JsonApiConnector(JsonApiConnectorConfig config, std::shared_ptr<HttpClient> http);

    std::string name() const override;
    FetchResult fetch(std::size_t record_limit) override;

private:
    JsonApiConnectorConfig config_;
    std::shared_ptr<HttpClient> http_;
};

} // namespace cat_osint
