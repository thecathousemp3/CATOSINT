#include "cat_osint/connectors/json_api_connector.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <utility>

namespace cat_osint {
namespace {

using Json = nlohmann::json;

const Json* at_path(const Json& value, const std::string& path) {
    const Json* current = &value;
    std::size_t start{};
    while (start < path.size()) {
        const auto end = path.find('.', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (current->is_object()) {
            const auto found = current->find(part);
            if (found == current->end()) {
                return nullptr;
            }
            current = &*found;
        } else if (current->is_array()) {
            std::size_t index{};
            try {
                index = static_cast<std::size_t>(std::stoull(part));
            } catch (...) {
                return nullptr;
            }
            if (index >= current->size()) {
                return nullptr;
            }
            current = &(*current)[index];
        } else {
            return nullptr;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return current;
}

std::string string_value(const Json* value) {
    if (value == nullptr || value->is_null()) {
        return {};
    }
    if (value->is_string()) {
        return value->get<std::string>();
    }
    if (value->is_primitive()) {
        return value->dump();
    }
    return {};
}

std::optional<std::chrono::seconds> retry_after(const HttpResponse& response) {
    const auto found = response.headers.find("retry-after");
    if (found == response.headers.end()) {
        return std::nullopt;
    }
    try {
        const auto value = std::stoll(found->second);
        if (value > 0) {
            return std::chrono::seconds(value);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

} // namespace

JsonApiConnector::JsonApiConnector(
    JsonApiConnectorConfig config,
    std::shared_ptr<HttpClient> http)
    : config_(std::move(config)), http_(std::move(http)) {
    if (config_.name.empty() || config_.url.empty() || config_.content_path.empty() ||
        config_.max_response_bytes == 0 || !http_) {
        throw std::invalid_argument("JSON API connector configuration is incomplete");
    }
}

std::string JsonApiConnector::name() const {
    return config_.name;
}

FetchResult JsonApiConnector::fetch(std::size_t record_limit) {
    FetchResult result;
    if (record_limit == 0) {
        return result;
    }

    HttpRequest request;
    request.url = config_.url;
    request.headers = config_.headers;
    request.headers.emplace("Accept", "application/json");
    request.max_response_bytes = config_.max_response_bytes;

    const auto response = http_->perform(request);
    result.retry_after = retry_after(response);
    if (response.status < 200 || response.status >= 300) {
        result.error = "Source returned HTTP " + std::to_string(response.status);
        return result;
    }

    const auto document = Json::parse(response.body);
    const Json* collection = config_.records_path.empty()
        ? &document
        : at_path(document, config_.records_path);
    if (collection == nullptr || !collection->is_array()) {
        result.error = "Configured records path did not resolve to a JSON array";
        return result;
    }

    const auto count = std::min(record_limit, collection->size());
    result.records.reserve(count);
    const auto observed_at = std::chrono::system_clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& item = (*collection)[i];
        Record record;
        record.source_id = config_.name;
        record.title = string_value(at_path(item, config_.title_path));
        record.content = string_value(at_path(item, config_.content_path));
        record.source_url = string_value(at_path(item, config_.source_url_path));
        if (record.source_url.empty()) {
            record.source_url = config_.url;
        }
        if (!config_.id_path.empty()) {
            const auto id = string_value(at_path(item, config_.id_path));
            if (!id.empty()) {
                record.source_id += ":" + id;
            }
        }
        record.observed_at = observed_at;
        if (!record.content.empty()) {
            result.records.push_back(std::move(record));
        }
    }
    return result;
}

} // namespace cat_osint
