#include <KingdomEngine/ImGui/ImMenu.h>
#include <KingdomEngine/ImGui/ImWindowManager.h>

namespace Editor
{
	class WindowMenu : public KE::ImMenu
	{
	public:
		WindowMenu() : KE::ImMenu("Window", true)
		{
		}

		void OnRender() override
		{
			for (KE::ImWindow* window : KE::ImWindowManager::Get().GetWindows())
			{
				const std::string& name = window->properties.name;
				if (ImGui::MenuItem(name.c_str(), nullptr, window->properties.isVisible))
					KE::ImWindowManager::Get().SetWindowVisible(name, !window->properties.isVisible);
			}
		}
	};

	WindowMenu windowMenu;
}
