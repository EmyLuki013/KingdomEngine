#ifndef LOGS_WINDOW_H
#define LOGS_WINDOW_H

#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/Core/Log.h>

#include <string>

namespace Editor
{
	class LogsWindow : public KE::ImWindow
	{
	public:
		LogsWindow() : KE::ImWindow("Logs", 0, 599, 1366, 136, true, false, KE::DockSide::DOWN)
		{
		}

		void OnReady() override
		{

		}

		void OnRender() override
		{
			if (ImGui::Button("Clear Logs"))
				KE::Log::ClearMessages();
			ImGui::SameLine();
			if (ImGui::Button("Copy Logs"))
			{
				const std::vector<KE::Log::Message> messages = KE::Log::GetLogMessages();
				std::string copiedLogs;
				for (const auto& message : messages)
				{
					copiedLogs.append(message.text);
					copiedLogs.push_back('\n');
				}
				ImGui::SetClipboardText(copiedLogs.c_str());
			}

			ImGui::Separator();

			const std::vector<KE::Log::Message> messages = KE::Log::GetLogMessages();
			ImGui::BeginChild("LogMessages", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
			const bool scrollToBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
			for (const auto& message : messages)
			{
				ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
				switch (message.level)
				{
				case spdlog::level::trace:
					color = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
					break;
				case spdlog::level::debug:
					color = ImVec4(0.55f, 0.75f, 0.9f, 1.0f);
					break;
				case spdlog::level::warn:
					color = ImVec4(1.0f, 0.8f, 0.25f, 1.0f);
					break;
				case spdlog::level::err:
					color = ImVec4(1.0f, 0.35f, 0.3f, 1.0f);
					break;
				case spdlog::level::critical:
					color = ImVec4(1.0f, 0.1f, 0.1f, 1.0f);
					break;
				default:
					break;
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::TextUnformatted(message.text.c_str());
				ImGui::PopStyleColor();
			}
			if (scrollToBottom)
				ImGui::SetScrollHereY(1.0f);
			ImGui::EndChild();
		}
	};
}

#endif
