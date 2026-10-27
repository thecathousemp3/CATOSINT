#include "cat_osint/connectors/json_api_connector.hpp"
#include "cat_osint/api/local_api_server.hpp"
#include "cat_osint/core/engine.hpp"
#include "cat_osint/models/openai_compatible_model.hpp"
#include "cat_osint/net/http_client.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {

std::atomic_bool running{true};

void stop(int) {
    running = false;
}

void print_help() {
    std::cout
        << "CAT OSINT 0.1.0\n"
        << "Poll one documented JSON API and analyze bounded records with a local model.\n\n"
        << "Required:\n"
        << "  --serve                   Run local engine API for the TypeScript UI\n"
        << "  --port N                  API port with --serve (default: 8765)\n"
        << "  --api-url URL             HTTPS endpoint returning JSON (single source)\n"
        << "  --sources-file PATH       JSON file configuring multiple API sources\n"
        << "  --model-url URL           Local OpenAI-compatible chat-completions endpoint\n"
        << "  --model NAME              Model identifier configured in the local server\n\n"
        << "Optional:\n"
        << "  --name NAME               Source name (default: configured-api)\n"
        << "  --records-path PATH       Dot path to response array (default: root array)\n"
        << "  --id-path PATH            Record ID path (default: id; empty disables)\n"
        << "  --title-path PATH         Record title path (default: title)\n"
        << "  --content-path PATH       Record content path (default: body)\n"
        << "  --source-url-path PATH    Record URL path (default: endpoint URL)\n"
        << "  --auth-env VAR            Read API token from this environment variable\n"
        << "  --auth-header NAME        Token header (default: Authorization)\n"
        << "  --auth-prefix TEXT        Token prefix (default: 'Bearer ')\n"
        << "  --interval-seconds N      Minimum per-source poll interval (default: 60)\n"
        << "                            (default interval for sources-file entries)\n"
        << "  --max-records N           Records per model request (default: 8)\n"
        << "  --max-content-chars N     Maximum content characters per record (default: 4000)\n"
        << "  --watch                   Repeat, respecting the configured source interval\n"
        << "  --help                    Show this help\n\n"
        << "Example (LM Studio):\n"
        << "  cat-osint --api-url https://example.org/api/items --model-url "
           "http://127.0.0.1:1234/v1/chat/completions --model local-model\n";
}

std::size_t positive_number(const std::string& value, const std::string& option) {
    std::size_t parsed{};
    std::size_t consumed{};
    try {
        parsed = std::stoull(value, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(option + " must be a positive whole number");
    }
    if (parsed == 0 || consumed != value.size()) {
        throw std::invalid_argument(option + " must be greater than zero");
    }
    return parsed;
}

std::string environment_value(const std::string& name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length{};
    const auto status = _dupenv_s(&value, &length, name.c_str());
    if (status != 0) {
        std::free(value);
        throw std::runtime_error("Could not read requested environment variable");
    }
    std::string result = value == nullptr ? "" : value;
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name.c_str());
    return value == nullptr ? "" : value;
#endif
}

std::string json_string(
    const nlohmann::json& object,
    const char* key,
    const std::string& fallback) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return fallback;
    }
    if (!found->is_string()) {
        throw std::invalid_argument(std::string("Source field must be a string: ") + key);
    }
    return found->get<std::string>();
}

void reject_unknown_source_fields(const nlohmann::json& source) {
    static const std::unordered_map<std::string, bool> allowed{
        {"name", true}, {"url", true}, {"records_path", true},
        {"id_path", true}, {"title_path", true}, {"content_path", true},
        {"source_url_path", true}, {"interval_seconds", true},
        {"auth_env", true}, {"auth_header", true}, {"auth_prefix", true}
    };
    for (auto it = source.begin(); it != source.end(); ++it) {
        if (!allowed.contains(it.key())) {
            throw std::invalid_argument("Unknown source configuration field: " + it.key());
        }
    }
}

nlohmann::json read_source_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open sources configuration file");
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0 || size > 1024 * 1024) {
        throw std::runtime_error("Sources configuration must be at most 1 MiB");
    }
    file.seekg(0, std::ios::beg);
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (!contents.empty()) {
        file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!file) {
            throw std::runtime_error("Could not read sources configuration file");
        }
    }
    return nlohmann::json::parse(contents);
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::unordered_map<std::string, std::string> options;
        bool watch = false;
        bool serve = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--help") {
                print_help();
                return 0;
            }
            if (argument == "--watch") {
                watch = true;
                continue;
            }
            if (argument == "--serve") {
                serve = true;
                continue;
            }
            if (argument.rfind("--", 0) != 0 || i + 1 >= argc) {
                throw std::invalid_argument("Invalid or incomplete option: " + argument);
            }
            if (!options.emplace(argument, argv[++i]).second) {
                throw std::invalid_argument("Option specified more than once: " + argument);
            }
        }

        const auto value_or = [&options](const std::string& key, const std::string& fallback) {
            const auto found = options.find(key);
            return found == options.end() ? fallback : found->second;
        };
        if (serve) {
            for (const auto& [key, value] : options) {
                (void)value;
                if (key != "--port") {
                    throw std::invalid_argument(
                        "--serve cannot be combined with source or model options");
                }
            }
            const auto port = positive_number(value_or("--port", "8765"), "--port");
            if (port > 65535) {
                throw std::invalid_argument("--port must be between 1 and 65535");
            }
            auto http = std::make_shared<cat_osint::WinHttpClient>();
            cat_osint::LocalApiServer server(std::move(http));
            return server.run(static_cast<unsigned short>(port)) ? 0 : 1;
        }
        if (options.contains("--port")) {
            throw std::invalid_argument("--port is only valid with --serve");
        }
        for (const auto& [key, value] : options) {
            (void)value;
            const bool known = key == "--api-url" || key == "--model-url" ||
                key == "--model" || key == "--name" || key == "--records-path" ||
                key == "--id-path" || key == "--title-path" ||
                key == "--content-path" || key == "--source-url-path" ||
                key == "--auth-env" || key == "--auth-header" ||
                key == "--auth-prefix" || key == "--interval-seconds" ||
                key == "--max-records" || key == "--max-content-chars" ||
                key == "--sources-file" || key == "--port";
            if (!known) {
                throw std::invalid_argument("Unknown option: " + key);
            }
        }
        const bool multi_source = options.contains("--sources-file");
        if ((multi_source && options.contains("--api-url")) ||
            (!multi_source && !options.contains("--api-url"))) {
            throw std::invalid_argument(
                "Specify exactly one of --api-url or --sources-file");
        }
        if (multi_source) {
            for (const auto& option : {
                     "--name", "--records-path", "--id-path", "--title-path",
                     "--content-path", "--source-url-path", "--auth-env",
                     "--auth-header", "--auth-prefix"}) {
                if (options.contains(option)) {
                    throw std::invalid_argument(
                        "Per-source options belong in the sources file");
                }
            }
        }
        if (!options.contains("--model-url") || !options.contains("--model")) {
            print_help();
            return 2;
        }

        auto http = std::make_shared<cat_osint::WinHttpClient>();
        const auto interval = positive_number(
            value_or("--interval-seconds", "60"), "--interval-seconds");
        if (interval > 31'536'000) {
            throw std::invalid_argument("--interval-seconds cannot exceed one year");
        }
        const auto max_records = positive_number(
            value_or("--max-records", "8"), "--max-records");
        const auto max_content = positive_number(
            value_or("--max-content-chars", "4000"), "--max-content-chars");

        cat_osint::OpenAiCompatibleModelConfig model_config;
        model_config.endpoint = options.at("--model-url");
        model_config.model = options.at("--model");
        model_config.api_key = environment_value("CAT_OSINT_LLM_API_KEY");

        cat_osint::Engine engine({
            max_records,
            max_content,
            std::chrono::seconds(interval)
        });
        if (multi_source) {
            const auto document = read_source_file(options.at("--sources-file"));
            if (!document.is_object() || !document.contains("sources") ||
                !document["sources"].is_array() || document["sources"].empty()) {
                throw std::invalid_argument(
                    "Sources file must contain a non-empty 'sources' array");
            }
            if (document.size() != 1) {
                throw std::invalid_argument(
                    "Sources file only accepts the top-level 'sources' property");
            }
            for (const auto& entry : document["sources"]) {
                if (!entry.is_object()) {
                    throw std::invalid_argument("Each source entry must be an object");
                }
                reject_unknown_source_fields(entry);
                cat_osint::JsonApiConnectorConfig source;
                source.name = json_string(entry, "name", "");
                source.url = json_string(entry, "url", "");
                source.records_path = json_string(entry, "records_path", "");
                source.id_path = json_string(entry, "id_path", "id");
                source.title_path = json_string(entry, "title_path", "title");
                source.content_path = json_string(entry, "content_path", "body");
                source.source_url_path = json_string(entry, "source_url_path", "");
                if (source.name.empty() || source.url.empty()) {
                    throw std::invalid_argument("Each source requires non-empty name and url");
                }

                auto source_interval = std::chrono::seconds(interval);
                if (entry.contains("interval_seconds")) {
                    if (!entry["interval_seconds"].is_number_integer() ||
                        entry["interval_seconds"].get<long long>() <= 0 ||
                        entry["interval_seconds"].get<long long>() > 31'536'000) {
                        throw std::invalid_argument(
                            "interval_seconds must be positive and no more than one year");
                    }
                    source_interval = std::chrono::seconds(
                        entry["interval_seconds"].get<long long>());
                }

                const auto auth_env = json_string(entry, "auth_env", "");
                if (!auth_env.empty()) {
                    const auto token = environment_value(auth_env);
                    if (token.empty()) {
                        throw std::runtime_error(
                            "A source API token environment variable is unset or empty");
                    }
                    source.headers.emplace(
                        json_string(entry, "auth_header", "Authorization"),
                        json_string(entry, "auth_prefix", "Bearer ") + token);
                }

                engine.add_connector(
                    std::make_shared<cat_osint::JsonApiConnector>(
                        std::move(source), http),
                    source_interval);
            }
        } else {
            cat_osint::JsonApiConnectorConfig source;
            source.name = value_or("--name", "configured-api");
            source.url = options.at("--api-url");
            source.records_path = value_or("--records-path", "");
            source.id_path = value_or("--id-path", "id");
            source.title_path = value_or("--title-path", "title");
            source.content_path = value_or("--content-path", "body");
            source.source_url_path = value_or("--source-url-path", "");
            const auto auth_env = options.find("--auth-env");
            if (auth_env != options.end()) {
                const auto token = environment_value(auth_env->second);
                if (token.empty()) {
                    throw std::runtime_error(
                        "The requested API token environment variable is unset or empty");
                }
                source.headers.emplace(
                    value_or("--auth-header", "Authorization"),
                    value_or("--auth-prefix", "Bearer ") + token);
            }
            engine.add_connector(
                std::make_shared<cat_osint::JsonApiConnector>(std::move(source), http));
        }
        engine.set_model(std::make_shared<cat_osint::OpenAiCompatibleModel>(
            std::move(model_config), http));

        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        do {
            const auto result = engine.run_once();
            for (const auto& status : result.sources) {
                if (!status.error.empty()) {
                    std::cerr << "Source " << status.source << ": "
                              << status.error << '\n';
                }
            }
            if (!result.analysis.empty()) {
                std::cout << "Analyzed " << result.records_analyzed
                          << " record(s)\n" << result.analysis << "\n\n";
            }
            if (watch && running) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } while (watch && running);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CAT OSINT error: " << error.what() << '\n';
        return 1;
    }
}
