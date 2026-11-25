#include "KingdomEngine/Core/Log.h"

#include <spdlog/sinks/base_sink.h>
#include "spdlog/sinks/stdout_color_sinks.h"
#include <spdlog/sinks/basic_file_sink.h>

#include <deque>
#include <mutex>

namespace KE
{
	namespace
	{
		class MemorySink final : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			std::vector<Log::Message> GetMessages()
			{
				std::lock_guard<std::mutex> lock(mutex_);
				return std::vector<Log::Message>(messages_.begin(), messages_.end());
			}

			void ClearMessages()
			{
				std::lock_guard<std::mutex> lock(mutex_);
				messages_.clear();
			}

		protected:
			void sink_it_(const spdlog::details::log_msg& message) override
			{
				spdlog::memory_buf_t formatted;
				formatter_->format(message, formatted);
				if (messages_.size() == maxMessages)
					messages_.pop_front();
				messages_.push_back(Log::Message{
					message.level,
					std::string(formatted.data(), formatted.size())
				});
			}

			void flush_() override
			{
			}

		private:
			static constexpr size_t maxMessages = 5000;
			std::deque<Log::Message> messages_;
		};

		std::shared_ptr<MemorySink> memorySink;
	}

	std::shared_ptr<spdlog::logger> Log::logger;

	void Log::Init(const std::string& logsFileName)
	{
		auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logsFileName, true);
		auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		memorySink = std::make_shared<MemorySink>();

		console_sink->set_pattern("%^[%T] %n: %v%$");
		file_sink->set_pattern("[%T] %n: %v");
		memorySink->set_pattern("[%T] %n: %v");

		logger = std::make_shared<spdlog::logger>("LOG", spdlog::sinks_init_list{ console_sink, file_sink, memorySink });
		spdlog::register_logger(logger);
		logger->set_level(spdlog::level::trace);
		logger->flush_on(spdlog::level::trace);
	}

	std::vector<std::string> Log::GetMessages()
	{
		const auto entries = GetLogMessages();
		std::vector<std::string> messages;
		messages.reserve(entries.size());
		for (const auto& entry : entries)
			messages.push_back(entry.text);
		return messages;
	}

	std::vector<Log::Message> Log::GetLogMessages()
	{
		return memorySink ? memorySink->GetMessages() : std::vector<Log::Message>{};
	}

	void Log::ClearMessages()
	{
		if (memorySink)
			memorySink->ClearMessages();
	}
}
