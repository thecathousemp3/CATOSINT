#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cat_osint {

struct Record {
    std::string source_id;
    std::string title;
    std::string content;
    std::string source_url;
    std::chrono::system_clock::time_point observed_at;
};

struct FetchResult {
    std::vector<Record> records;
    std::optional<std::chrono::seconds> retry_after;
    std::string error;
};

class PublicApiConnector {
public:
    virtual ~PublicApiConnector() = default;
    virtual std::string name() const = 0;
    virtual FetchResult fetch(std::size_t record_limit) = 0;
};

class LocalModel {
public:
    virtual ~LocalModel() = default;
    virtual std::string analyze(const std::vector<Record>& records) = 0;
};

struct EngineConfig {
    std::size_t max_records_per_analysis{32};
    std::size_t max_content_characters_per_record{4000};
    std::chrono::seconds default_source_interval{60};
};

struct SourceStatus {
    std::string source;
    std::size_t records_received{};
    bool skipped_by_rate_limit{};
    bool skipped_by_batch_limit{};
    std::string error;
};

struct EngineResult {
    std::vector<SourceStatus> sources;
    std::vector<Record> records;
    std::size_t records_analyzed{};
    std::string analysis;
};

class Engine {
public:
    explicit Engine(EngineConfig config = {});

    void add_connector(
        std::shared_ptr<PublicApiConnector> connector,
        std::optional<std::chrono::seconds> minimum_interval = std::nullopt);
    void set_model(std::shared_ptr<LocalModel> model);
    EngineResult run_once();

private:
    struct ConnectorState {
        std::shared_ptr<PublicApiConnector> connector;
        std::chrono::steady_clock::time_point next_allowed;
        std::chrono::seconds minimum_interval;
    };

    EngineConfig config_;
    std::vector<ConnectorState> connectors_;
    std::shared_ptr<LocalModel> model_;
};

} // namespace cat_osint
