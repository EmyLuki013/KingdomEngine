#ifndef LOGS_WINDOW_H
#define LOGS_WINDOW_H

#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/Core/Log.h>

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

			ImGui::Separator();

			const std::vector<std::string> messages = KE::Log::GetMessages();
			ImGui::BeginChild("LogMessages", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
			const bool scrollToBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
			for (const std::string& message : messages)
				ImGui::TextUnformatted(message.c_str());
			if (scrollToBottom)
				ImGui::SetScrollHereY(1.0f);
			ImGui::EndChild();
		}
	};
}

#endif
