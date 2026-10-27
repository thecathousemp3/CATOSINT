#include "cat_osint/api/local_api_server.hpp"

#include "cat_osint/models/local_model_discovery.hpp"
#include "cat_osint/models/openai_compatible_model.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace cat_osint {
namespace {

using Json = nlohmann::json;

bool allowed_origin(const std::string& origin) {
    return origin == "http://127.0.0.1:5173" ||
           origin == "http://localhost:5173" ||
           origin == "http://127.0.0.1:4173" ||
           origin == "http://localhost:4173";
}

void set_cors(const httplib::Request& request, httplib::Response& response) {
    const auto origin = request.get_header_value("Origin");
    if (allowed_origin(origin)) {
        response.set_header("Access-Control-Allow-Origin", origin);
        response.set_header("Vary", "Origin");
        response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        response.set_header("Access-Control-Allow-Headers", "Content-Type");
    }
}

void json_error(
    const httplib::Request& request,
    httplib::Response& response,
    int status,
    const std::string& message) {
    set_cors(request, response);
    response.status = status;
    response.set_content(Json{{"error", message}}.dump(), "application/json");
}

std::string required_string(const Json& value, const char* field) {
    const auto found = value.find(field);
    if (found == value.end() || !found->is_string() || found->get<std::string>().empty()) {
        throw std::invalid_argument(std::string("Field must be a non-empty string: ") + field);
    }
    return found->get<std::string>();
}

std::string optional_string(
    const Json& value,
    const char* field,
    const std::string& fallback) {
    const auto found = value.find(field);
    if (found == value.end()) {
        return fallback;
    }
    if (!found->is_string()) {
        throw std::invalid_argument(std::string("Field must be a string: ") + field);
    }
    return found->get<std::string>();
}

void validate_source_fields(const Json& value) {
    static const std::vector<std::string> allowed{
        "name", "url", "records_path", "id_path", "title_path",
        "content_path", "source_url_path", "interval_seconds",
        "auth_header", "auth_prefix", "api_key"
    };
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end()) {
            throw std::invalid_argument("Unsupported source field: " + it.key());
        }
    }
}

Json source_json(const JsonApiConnectorConfig& source, std::chrono::seconds interval) {
    return {
        {"name", source.name},
        {"url", source.url},
        {"records_path", source.records_path},
        {"id_path", source.id_path},
        {"title_path", source.title_path},
        {"content_path", source.content_path},
        {"source_url_path", source.source_url_path},
        {"interval_seconds", interval.count()},
        {"authentication_configured", !source.headers.empty()},
        {"status", "configured"}
    };
}

} // namespace

LocalApiServer::LocalApiServer(std::shared_ptr<HttpClient> http)
    : http_(std::move(http)), engine_(EngineConfig{32, 4000, std::chrono::seconds(60)}) {
    if (!http_) {
        throw std::invalid_argument("Local API server requires an HTTP client");
    }
    JsonApiConnectorConfig default_source;
    default_source.name = "USGS earthquakes (24h)";
    default_source.url =
        "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_day.geojson";
    default_source.records_path = "features";
    default_source.id_path = "id";
    default_source.title_path = "properties.title";
    default_source.content_path = "properties.title";
    default_source.source_url_path = "properties.url";
    engine_.add_connector(
        std::make_shared<JsonApiConnector>(default_source, http_),
        std::chrono::seconds(60));
    sources_.push_back(std::move(default_source));
    source_intervals_.push_back(std::chrono::seconds(60));

    JsonApiConnectorConfig weather_source;
    weather_source.name = "NWS active weather alerts";
    weather_source.url = "https://api.weather.gov/alerts/active";
    weather_source.records_path = "features";
    weather_source.id_path = "id";
    weather_source.title_path = "properties.headline";
    weather_source.content_path = "properties.description";
    weather_source.source_url_path = "id";
    engine_.add_connector(
        std::make_shared<JsonApiConnector>(weather_source, http_),
        std::chrono::seconds(300));
    sources_.push_back(std::move(weather_source));
    source_intervals_.push_back(std::chrono::seconds(300));

    JsonApiConnectorConfig earth_observation_source;
    earth_observation_source.name = "NASA EONET open events";
    earth_observation_source.url =
        "https://eonet.gsfc.nasa.gov/api/v3/events?status=open&limit=100";
    earth_observation_source.records_path = "events";
    earth_observation_source.id_path = "id";
    earth_observation_source.title_path = "title";
    earth_observation_source.content_path = "title";
    earth_observation_source.source_url_path = "link";
    engine_.add_connector(
        std::make_shared<JsonApiConnector>(earth_observation_source, http_),
        std::chrono::seconds(600));
    sources_.push_back(std::move(earth_observation_source));
    source_intervals_.push_back(std::chrono::seconds(600));
}

bool LocalApiServer::run(unsigned short port) {
    httplib::Server server;
    server.set_payload_max_length(32 * 1024);

    server.Get("/api/v1/health", [](const httplib::Request& request, httplib::Response& response) {
        set_cors(request, response);
        response.set_content(
            R"({"status":"ok","engine":"cat-osint","version":"0.1.0"})",
            "application/json");
    });

    server.Get("/api/v1/models", [this](const httplib::Request& request, httplib::Response& response) {
        set_cors(request, response);
        const auto base_url = request.has_param("server_url")
            ? request.get_param_value("server_url")
            : std::string{};
        const auto discovered = LocalModelDiscovery(http_).scan(base_url);
        Json models = Json::array();
        for (const auto& model : discovered.models) {
            models.push_back({
                {"provider", model.provider},
                {"model_id", model.model_id},
                {"completion_endpoint", model.completion_endpoint}
            });
        }
        response.set_content(
            Json{{"models", std::move(models)}, {"diagnostics", discovered.diagnostics}}.dump(),
            "application/json");
    });

    server.Get("/api/v1/sources", [this](const httplib::Request& request, httplib::Response& response) {
        set_cors(request, response);
        std::lock_guard lock(state_mutex_);
        Json sources = Json::array();
        for (std::size_t i = 0; i < sources_.size(); ++i) {
            sources.push_back(source_json(sources_[i], source_intervals_[i]));
        }
        response.set_content(Json{{"sources", std::move(sources)}}.dump(), "application/json");
    });

    server.Post("/api/v1/sources", [this](const httplib::Request& request, httplib::Response& response) {
        Json document;
        try {
            document = Json::parse(request.body);
            if (!document.is_object()) {
                throw std::invalid_argument("Source configuration must be a JSON object");
            }
            validate_source_fields(document);
            JsonApiConnectorConfig source;
            source.name = required_string(document, "name");
            source.url = required_string(document, "url");
            if (source.url.rfind("https://", 0) != 0) {
                throw std::invalid_argument("Source endpoint must use HTTPS");
            }
            const auto authority_end = source.url.find('/', 8);
            const auto authority = source.url.substr(
                8,
                authority_end == std::string::npos ? std::string::npos : authority_end - 8);
            if (authority.find('@') != std::string::npos ||
                source.url.find('#') != std::string::npos) {
                throw std::invalid_argument(
                    "Source URL must not embed credentials or a fragment");
            }
            source.records_path = optional_string(document, "records_path", "");
            source.id_path = optional_string(document, "id_path", "id");
            source.title_path = optional_string(document, "title_path", "title");
            source.content_path = optional_string(document, "content_path", "body");
            source.source_url_path = optional_string(document, "source_url_path", "");
            const auto auth_header = optional_string(document, "auth_header", "");
            const auto auth_prefix = optional_string(document, "auth_prefix", "Bearer ");
            const auto api_key = optional_string(document, "api_key", "");
            if (!auth_header.empty() && auth_header.find_first_of("\r\n:") != std::string::npos) {
                throw std::invalid_argument("auth_header must be a valid HTTP header name");
            }
            if (auth_prefix.find_first_of("\r\n") != std::string::npos ||
                api_key.find_first_of("\r\n") != std::string::npos) {
                throw std::invalid_argument("API credentials must not contain line breaks");
            }
            if (!api_key.empty()) {
                if (auth_header.empty()) {
                    source.headers.emplace("Authorization", auth_prefix + api_key);
                } else {
                    source.headers.emplace(auth_header, auth_prefix + api_key);
                }
            } else if (!auth_header.empty()) {
                throw std::invalid_argument("api_key is required when auth_header is set");
            }

            auto interval = std::chrono::seconds(60);
            const auto interval_it = document.find("interval_seconds");
            if (interval_it != document.end()) {
                if (!interval_it->is_number_integer() ||
                    interval_it->get<long long>() <= 0 ||
                    interval_it->get<long long>() > 31'536'000) {
                    throw std::invalid_argument(
                        "interval_seconds must be positive and no more than one year");
                }
                interval = std::chrono::seconds(interval_it->get<long long>());
            }
            if (source.content_path.empty()) {
                throw std::invalid_argument("content_path must not be empty");
            }

            std::lock_guard lock(state_mutex_);
            if (sources_.size() >= 20) {
                throw std::invalid_argument("A maximum of 20 sources can be configured");
            }
            for (const auto& configured : sources_) {
                if (configured.name == source.name) {
                    throw std::invalid_argument("A source with that name is already configured");
                }
            }
            auto connector = std::make_shared<JsonApiConnector>(source, http_);
            engine_.add_connector(std::move(connector), interval);
            sources_.push_back(source);
            source_intervals_.push_back(interval);

            set_cors(request, response);
            response.status = 201;
            response.set_content(source_json(source, interval).dump(), "application/json");
        } catch (const nlohmann::json::exception& error) {
            json_error(request, response, 400, std::string("Invalid source JSON: ") + error.what());
        } catch (const std::invalid_argument& error) {
            json_error(request, response, 400, error.what());
        }
    });

    server.Post("/api/v1/analyze", [this](const httplib::Request& request, httplib::Response& response) {
        std::string model_id;
        std::string model_server_url;
        try {
            const auto body = Json::parse(request.body);
            if (!body.is_object()) {
                throw std::invalid_argument("Analysis request must be a JSON object");
            }
            model_id = required_string(body, "model_id");
            model_server_url = optional_string(body, "server_url", "");
        } catch (const nlohmann::json::exception& error) {
            json_error(request, response, 400, std::string("Invalid request JSON: ") + error.what());
            return;
        } catch (const std::invalid_argument& error) {
            json_error(request, response, 400, error.what());
            return;
        }

        const auto discovery = LocalModelDiscovery(http_).scan(model_server_url);
        const auto model = std::find_if(
            discovery.models.begin(), discovery.models.end(),
            [&model_id](const DetectedModel& detected) {
                return detected.model_id == model_id;
            });
        if (model == discovery.models.end()) {
            json_error(request, response, 409, "Selected model is no longer available locally");
            return;
        }

        try {
            std::lock_guard lock(state_mutex_);
            if (sources_.empty()) {
                json_error(request, response, 409, "Configure at least one API source first");
                return;
            }
            OpenAiCompatibleModelConfig model_config;
            model_config.endpoint = model->completion_endpoint;
            model_config.model = model->model_id;
            engine_.set_model(std::make_shared<OpenAiCompatibleModel>(
                std::move(model_config), http_));
            const auto result = engine_.run_once();
            latest_records_ = result.records;
            latest_source_status_ = result.sources;
            latest_analysis_ = result.analysis;

            Json observations = Json::array();
            for (const auto& record : result.records) {
                observations.push_back({
                    {"source_id", record.source_id},
                    {"title", record.title},
                    {"content", record.content},
                    {"source_url", record.source_url},
                    {"observed_at_epoch_seconds",
                     std::chrono::duration_cast<std::chrono::seconds>(
                         record.observed_at.time_since_epoch()).count()}
                });
            }
            Json source_status = Json::array();
            for (const auto& status : result.sources) {
                source_status.push_back({
                    {"source", status.source},
                    {"records_received", status.records_received},
                    {"skipped_by_rate_limit", status.skipped_by_rate_limit},
                    {"skipped_by_batch_limit", status.skipped_by_batch_limit},
                    {"error", status.error}
                });
            }
            set_cors(request, response);
            response.set_content(
                Json{
                    {"records_analyzed", result.records_analyzed},
                    {"analysis", result.analysis},
                    {"observations", std::move(observations)},
                    {"sources", std::move(source_status)}
                }.dump(),
                "application/json");
        } catch (const std::exception& error) {
            json_error(request, response, 502, std::string("Analysis failed: ") + error.what());
        }
    });

    server.Get("/api/v1/observations", [this](const httplib::Request& request, httplib::Response& response) {
        set_cors(request, response);
        std::lock_guard lock(state_mutex_);
        Json observations = Json::array();
        for (const auto& record : latest_records_) {
            observations.push_back({
                {"source_id", record.source_id},
                {"title", record.title},
                {"content", record.content},
                {"source_url", record.source_url},
                {"observed_at_epoch_seconds",
                 std::chrono::duration_cast<std::chrono::seconds>(
                     record.observed_at.time_since_epoch()).count()}
            });
        }
        response.set_content(
            Json{{"observations", std::move(observations)}, {"analysis", latest_analysis_}}.dump(),
            "application/json");
    });

    server.Get("/api/v1/.*", [](const httplib::Request& request, httplib::Response& response) {
        json_error(request, response, 404, "Local engine API route not found");
    });
    server.Options(R"(/api/v1/.*)", [](const httplib::Request& request, httplib::Response& response) {
        set_cors(request, response);
        response.status = 204;
    });

    server.set_payload_max_length(32 * 1024);
    std::cout << "CAT OSINT local API listening at http://127.0.0.1:"
              << port << "/api/v1/health\n";
    if (!server.listen("127.0.0.1", port)) {
        std::cerr << "Could not start local API server on port " << port << '\n';
        return false;
    }
    return true;
}

} // namespace cat_osint
