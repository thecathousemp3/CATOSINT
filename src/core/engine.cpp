#include "cat_osint/core/engine.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cat_osint {

Engine::Engine(EngineConfig config) : config_(config) {
    if (config_.max_records_per_analysis == 0 ||
        config_.max_content_characters_per_record == 0 ||
        config_.default_source_interval <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("Engine limits and source interval must be positive");
    }
}

void Engine::add_connector(
    std::shared_ptr<PublicApiConnector> connector,
    std::optional<std::chrono::seconds> minimum_interval) {
    if (!connector) {
        throw std::invalid_argument("Connector must not be null");
    }
    const auto interval = minimum_interval.value_or(config_.default_source_interval);
    if (interval <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("Connector poll interval must be positive");
    }
    connectors_.push_back({
        std::move(connector),
        std::chrono::steady_clock::time_point::min(),
        interval
    });
}

void Engine::set_model(std::shared_ptr<LocalModel> model) {
    if (!model) {
        throw std::invalid_argument("Local model must not be null");
    }
    model_ = std::move(model);
}

EngineResult Engine::run_once() {
    if (!model_) {
        throw std::logic_error("A local model must be configured before running the engine");
    }

    EngineResult result;
    std::vector<Record> records;
    const auto now = std::chrono::steady_clock::now();

    for (std::size_t index = 0; index < connectors_.size(); ++index) {
        auto& state = connectors_[index];
        SourceStatus status;
        status.source = state.connector->name();
        if (records.size() == config_.max_records_per_analysis) {
            status.skipped_by_batch_limit = true;
            result.sources.push_back(std::move(status));
            continue;
        }
        if (now < state.next_allowed) {
            status.skipped_by_rate_limit = true;
            result.sources.push_back(std::move(status));
            continue;
        }

        try {
            const auto remaining = config_.max_records_per_analysis - records.size();
            std::size_t ready_connectors = 0;
            for (std::size_t next = index; next < connectors_.size(); ++next) {
                if (now >= connectors_[next].next_allowed) {
                    ++ready_connectors;
                }
            }
            const auto fair_share =
                (remaining + ready_connectors - 1) / ready_connectors;
            auto fetched = state.connector->fetch(fair_share);
            status.error = std::move(fetched.error);
            status.records_received = fetched.records.size();
            auto delay = state.minimum_interval;
            if (fetched.retry_after && *fetched.retry_after > delay) {
                delay = *fetched.retry_after;
            }
            state.next_allowed = std::chrono::steady_clock::now() + delay;

            for (auto& record : fetched.records) {
                if (records.size() == config_.max_records_per_analysis) {
                    break;
                }
                if (record.content.size() > config_.max_content_characters_per_record) {
                    record.content.resize(config_.max_content_characters_per_record);
                }
                records.push_back(std::move(record));
            }
        } catch (const std::exception& error) {
            status.error = error.what();
            state.next_allowed = std::chrono::steady_clock::now() + state.minimum_interval;
        } catch (...) {
            status.error = "Connector failed with an unknown error";
            state.next_allowed = std::chrono::steady_clock::now() + state.minimum_interval;
        }
        result.sources.push_back(std::move(status));
    }

    if (!records.empty()) {
        result.analysis = model_->analyze(records);
        result.records_analyzed = records.size();
        result.records = std::move(records);
    }
    return result;
}

} // namespace cat_osint
