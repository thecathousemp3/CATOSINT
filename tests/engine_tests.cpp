#include "cat_osint/connectors/json_api_connector.hpp"
#include "cat_osint/core/engine.hpp"
#include "cat_osint/models/local_model_discovery.hpp"
#include "cat_osint/models/openai_compatible_model.hpp"

#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TestConnector final : public cat_osint::PublicApiConnector {
public:
    std::string name() const override { return "test-source"; }

    cat_osint::FetchResult fetch(std::size_t limit) override {
        ++calls;
        last_limit = limit;
        cat_osint::FetchResult result;
        for (std::size_t i = 0; i < limit; ++i) {
            result.records.push_back({"test-source", "title", std::string(20, 'x'), "https://example.test",
                                      std::chrono::system_clock::now()});
        }
        return result;
    }

    std::size_t calls{};
    std::size_t last_limit{};
};

class TestModel final : public cat_osint::LocalModel {
public:
    std::string analyze(const std::vector<cat_osint::Record>& records) override {
        received = records;
        return "test analysis";
    }

    std::vector<cat_osint::Record> received;
};

class FakeHttpClient final : public cat_osint::HttpClient {
public:
    cat_osint::HttpResponse perform(const cat_osint::HttpRequest& request) override {
        last_request = request;
        ++calls;
        const auto url_response = url_responses.find(request.url);
        if (url_response != url_responses.end()) {
            return url_response->second;
        }
        return response;
    }

    cat_osint::HttpResponse response;
    std::map<std::string, cat_osint::HttpResponse> url_responses;
    cat_osint::HttpRequest last_request;
    std::size_t calls{};
};

void test_engine_limits_and_interval() {
    auto connector = std::make_shared<TestConnector>();
    auto second_connector = std::make_shared<TestConnector>();
    auto model = std::make_shared<TestModel>();
    cat_osint::Engine engine({2, 8, std::chrono::seconds(60)});
    engine.add_connector(connector);
    engine.add_connector(second_connector);
    engine.set_model(model);

    const auto first = engine.run_once();
    require(first.records_analyzed == 2, "engine must cap record count");
    require(first.analysis == "test analysis", "engine must return model analysis");
    require(model->received.size() == 2, "model must receive bounded records");
    require(model->received.front().content.size() == 8, "engine must truncate record content");
    require(connector->last_limit == 1, "first connector must receive a fair share of batch capacity");
    require(second_connector->last_limit == 1,
            "second connector must receive a fair share of batch capacity");
    require(connector->calls == 1 && second_connector->calls == 1,
            "all ready sources must contribute to a bounded batch");
    require(!first.sources[1].skipped_by_batch_limit,
            "a ready source must not be skipped while fair batch capacity remains");

    const auto second = engine.run_once();
    require(second.records_analyzed == 0, "rate-limited sources must not be fetched again");
    require(second.sources.front().skipped_by_rate_limit, "connector must observe minimum poll interval");
    require(connector->calls == 1, "rate-limited connector must not be called again");
    require(second.sources[1].skipped_by_rate_limit,
            "all fetched sources must observe their minimum polling interval");
    require(second_connector->calls == 1, "rate-limited connector must not be called again");
}

void test_json_api_connector() {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.status = 200;
    http->response.headers["retry-after"] = "120";
    http->response.body = R"({"data":[{"id":7,"headline":"Update","text":"Details","link":"https://example.test/7"}]})";

    cat_osint::JsonApiConnectorConfig config;
    config.name = "example-api";
    config.url = "https://example.test/api";
    config.records_path = "data";
    config.title_path = "headline";
    config.content_path = "text";
    config.source_url_path = "link";
    config.headers["X-API-Key"] = "test-secret";
    cat_osint::JsonApiConnector connector(config, http);
    const auto fetched = connector.fetch(1);

    require(fetched.records.size() == 1, "JSON connector must parse configured array path");
    require(fetched.records.front().source_id == "example-api:7", "JSON connector must retain record ID");
    require(fetched.records.front().title == "Update", "JSON connector must map title");
    require(fetched.records.front().content == "Details", "JSON connector must map content");
    require(fetched.records.front().source_url == "https://example.test/7", "JSON connector must retain source URL");
    require(fetched.retry_after == std::chrono::seconds(120), "connector must parse Retry-After seconds");
    require(http->last_request.url == config.url, "connector must request configured endpoint");
    require(http->last_request.headers.at("X-API-Key") == "test-secret",
            "JSON connector must send configured API credentials");
}

void test_local_model_adapter() {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.status = 200;
    http->response.body = R"({"choices":[{"message":{"content":"Sourced summary"}}]})";

    cat_osint::OpenAiCompatibleModelConfig config;
    config.endpoint = "http://127.0.0.1:1234/v1/chat/completions";
    config.model = "local-test-model";
    cat_osint::OpenAiCompatibleModel model(config, http);

    cat_osint::Record record{
        "source:7", "Update", "Details", "https://example.test/7",
        std::chrono::system_clock::now()
    };
    const auto analysis = model.analyze({record});

    require(analysis == "Sourced summary", "model adapter must extract completion content");
    require(http->last_request.method == "POST", "model adapter must use POST");
    require(http->last_request.allow_http_loopback, "model adapter must permit local HTTP endpoint");
    require(http->last_request.loopback_only, "model adapter must reject non-loopback hosts");
    require(http->last_request.body.find("source:7") != std::string::npos,
            "model request must include source attribution");
    require(http->last_request.body.find("untrusted data") != std::string::npos,
            "model prompt must treat source content as untrusted");
}

void test_local_model_discovery() {
    auto http = std::make_shared<FakeHttpClient>();
    cat_osint::HttpResponse lm_studio;
    lm_studio.status = 200;
    lm_studio.body = R"({"data":[{"id":"qwen3.5-3.59b"}]})";
    cat_osint::HttpResponse ollama;
    ollama.status = 200;
    ollama.body = R"({"models":[{"name":"qwen3.5:3.59b"}]})";
    cat_osint::HttpResponse bionic;
    bionic.status = 200;
    bionic.body = R"({"data":[{"id":"qwen/qwen3.5-9b"}]})";
    http->url_responses["http://127.0.0.1:1234/v1/models"] = lm_studio;
    http->url_responses["http://127.0.0.1:11434/api/tags"] = ollama;
    http->url_responses["http://127.0.0.1:1235/v1/models"] = bionic;

    const auto discovered = cat_osint::LocalModelDiscovery(http).scan();
    require(discovered.models.size() == 3, "discovery must find models from known local providers");
    require(discovered.models[0].provider == "LM Studio", "discovery must identify LM Studio");
    require(discovered.models[0].model_id == "qwen3.5-3.59b", "discovery must preserve LM Studio model ID");
    require(discovered.models[1].provider == "Ollama", "discovery must identify Ollama");
    require(discovered.models[1].model_id == "qwen3.5:3.59b", "discovery must preserve Ollama model name");
    require(discovered.models[2].provider == "Bionic", "discovery must identify Bionic");
    require(discovered.models[2].model_id == "qwen/qwen3.5-9b",
            "discovery must preserve Bionic model ID");
    require(http->calls == 8,
            "discovery must probe the documented loopback endpoints for supported providers");
    require(http->last_request.loopback_only, "provider discovery must remain loopback-only");
}

void test_custom_local_model_discovery() {
    auto http = std::make_shared<FakeHttpClient>();
    cat_osint::HttpResponse custom_server;
    custom_server.status = 200;
    custom_server.body = R"({"data":[{"id":"qwen3.5-3.59b"}]})";
    http->url_responses["http://127.0.0.1:2345/v1/models"] = custom_server;

    const auto discovered =
        cat_osint::LocalModelDiscovery(http).scan("http://127.0.0.1:2345/v1/");
    require(discovered.models.size() == 1,
            "custom local discovery must include an OpenAI-compatible server");
    require(discovered.models.front().provider == "OpenAI-compatible local server",
            "custom local discovery must identify the compatible server");
    require(discovered.models.front().completion_endpoint ==
                "http://127.0.0.1:2345/v1/chat/completions",
            "custom local discovery must build the completion endpoint");
    require(http->calls == 9, "custom discovery must probe the configured URL once");
    require(http->last_request.url == "http://127.0.0.1:2345/v1/models",
            "custom discovery must request the configured loopback server");
    require(http->last_request.loopback_only,
            "custom model discovery must remain loopback-only");
}

} // namespace

int main() {
    try {
        test_engine_limits_and_interval();
        test_json_api_connector();
        test_local_model_adapter();
        test_local_model_discovery();
        test_custom_local_model_discovery();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
