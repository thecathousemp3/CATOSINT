#include "cat_osint/models/local_model_discovery.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cat_osint {
namespace {

using Json = nlohmann::json;

HttpRequest local_get(std::string url) {
    HttpRequest request;
    request.url = std::move(url);
    request.timeout = std::chrono::milliseconds(700);
    request.max_response_bytes = 1024 * 1024;
    request.allow_http_loopback = true;
    request.loopback_only = true;
    return request;
}

void add_model(
    ModelDiscoveryResult& result,
    const std::string& provider,
    const std::string& model_id,
    const std::string& endpoint) {
    if (model_id.empty()) {
        return;
    }
    result.models.push_back({provider, model_id, endpoint});
}

std::string trim_trailing_slashes(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

void probe_openai_compatible_server(
    HttpClient& http,
    ModelDiscoveryResult& result,
    const std::string& provider,
    const std::string& base_url,
    bool include_diagnostic) {
    auto normalized_base = trim_trailing_slashes(base_url);
    if (normalized_base.size() >= 3 &&
        normalized_base.compare(normalized_base.size() - 3, 3, "/v1") == 0) {
        normalized_base.resize(normalized_base.size() - 3);
    }
    const auto models_url = normalized_base + "/v1/models";
    const auto completion_url = normalized_base + "/v1/chat/completions";
    try {
        const auto response = http.perform(local_get(models_url));
        if (response.status != 200) {
            if (include_diagnostic) {
                result.diagnostics.emplace_back(
                    provider + " was not detected at " + normalized_base);
            }
            return;
        }

        const auto body = Json::parse(response.body);
        if (!body.contains("data") || !body["data"].is_array()) {
            if (include_diagnostic) {
                result.diagnostics.emplace_back(
                    provider + " responded, but its /v1/models response was not recognized");
            }
            return;
        }
        for (const auto& model : body["data"]) {
            if (!model.is_object() || !model.contains("id") || !model["id"].is_string()) {
                continue;
            }
            const auto model_id = model["id"].get<std::string>();
            const auto duplicate = std::find_if(
                result.models.begin(), result.models.end(),
                [&model_id, &completion_url](const DetectedModel& detected) {
                    return detected.model_id == model_id &&
                           detected.completion_endpoint == completion_url;
                });
            if (duplicate == result.models.end()) {
                add_model(result, provider, model_id, completion_url);
            }
        }
    } catch (const std::exception&) {
        if (include_diagnostic) {
            result.diagnostics.emplace_back(
                provider + " was not detected at " + normalized_base);
        }
    }
}

} // namespace

LocalModelDiscovery::LocalModelDiscovery(std::shared_ptr<HttpClient> http)
    : http_(std::move(http)) {
    if (!http_) {
        throw std::invalid_argument("Local model discovery requires an HTTP client");
    }
}

ModelDiscoveryResult LocalModelDiscovery::scan(
    const std::string& openai_compatible_base_url) const {
    ModelDiscoveryResult result;

    try {
        const auto response = http_->perform(local_get("http://127.0.0.1:1234/v1/models"));
        if (response.status == 200) {
            const auto body = Json::parse(response.body);
            if (body.contains("data") && body["data"].is_array()) {
                for (const auto& model : body["data"]) {
                    if (model.is_object() && model.contains("id") &&
                        model["id"].is_string()) {
                        add_model(
                            result,
                            "LM Studio",
                            model["id"].get<std::string>(),
                            "http://127.0.0.1:1234/v1/chat/completions");
                    }
                }
            } else {
                result.diagnostics.emplace_back(
                    "LM Studio responded, but its /v1/models response was not recognized");
            }
        } else {
            result.diagnostics.emplace_back(
                "LM Studio was not detected at 127.0.0.1:1234");
        }
    } catch (const std::exception&) {
        result.diagnostics.emplace_back(
            "LM Studio was not detected at 127.0.0.1:1234");
    }

    try {
        const auto response = http_->perform(local_get("http://127.0.0.1:11434/api/tags"));
        if (response.status == 200) {
            const auto body = Json::parse(response.body);
            if (body.contains("models") && body["models"].is_array()) {
                for (const auto& model : body["models"]) {
                    if (model.is_object() && model.contains("name") &&
                        model["name"].is_string()) {
                        add_model(
                            result,
                            "Ollama",
                            model["name"].get<std::string>(),
                            "http://127.0.0.1:11434/v1/chat/completions");
                    }
                }
            } else {
                result.diagnostics.emplace_back(
                    "Ollama responded, but its /api/tags response was not recognized");
            }
        } else {
            result.diagnostics.emplace_back(
                "Ollama was not detected at 127.0.0.1:11434");
        }
    } catch (const std::exception&) {
        result.diagnostics.emplace_back(
            "Ollama was not detected at 127.0.0.1:11434");
    }

    const std::vector<std::pair<std::string, std::string>> compatible_servers{
        {"Bionic", "http://127.0.0.1:1235"},
        {"Jan", "http://127.0.0.1:1337"},
        {"GPT4All", "http://127.0.0.1:4891"},
        {"LocalAI / llama.cpp", "http://127.0.0.1:8080"},
        {"text-generation-webui", "http://127.0.0.1:5000"},
        {"KoboldCpp", "http://127.0.0.1:5001"}
    };
    for (const auto& [provider, base_url] : compatible_servers) {
        probe_openai_compatible_server(*http_, result, provider, base_url, false);
    }

    if (!openai_compatible_base_url.empty()) {
        const auto base_url = trim_trailing_slashes(openai_compatible_base_url);
        const std::vector<std::string> standard_bases{
            "http://127.0.0.1:1234",
            "http://127.0.0.1:11434",
            "http://127.0.0.1:1235",
            "http://localhost:1235",
            "http://127.0.0.1:1337",
            "http://127.0.0.1:4891",
            "http://127.0.0.1:8080",
            "http://127.0.0.1:5000",
            "http://127.0.0.1:5001"
        };
        const auto already_checked = std::find(
            standard_bases.begin(), standard_bases.end(), base_url) !=
            standard_bases.end();
        if (!already_checked) {
            probe_openai_compatible_server(
                *http_, result, "OpenAI-compatible local server", base_url, true);
        }
    }

    if (result.models.empty()) {
        result.diagnostics.emplace_back(
            "No models detected on supported local API endpoints. Start a local model API server to enable analysis.");
    }

    return result;
}

} // namespace cat_osint
