#include <KingdomEngine/ImGui/ImMenu.h>
#include <KingdomEngine/Core/Application.h>

#include "KingdomEditor/Project/ProjectManager.h"

namespace Editor
{
	class RunMenu : public KE::ImMenu
	{
	public:
		RunMenu() : KE::ImMenu("Run", true)
		{
		}

		void OnRender() override
		{
			ProjectManager& projectManager = ProjectManager::Get();
			KE::Application* application = KE::Application::GetInstance();
			const bool isRunning = application && application->IsRuntimeSceneRunning();
			if (ImGui::MenuItem("Run", "F5", false,
				projectManager.GetLoadedProject() != nullptr && !isRunning))
				projectManager.RunProject();
			if (ImGui::MenuItem("Stop", "Shift+F5", false, isRunning))
				projectManager.StopProject();
		}
	};

	RunMenu runMenu;
}
