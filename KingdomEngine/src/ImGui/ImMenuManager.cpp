#include "KingdomEngine/ImGui/ImMenuManager.h"

#include <algorithm>

namespace KE
{
	ImMenuManager &ImMenuManager::Get()
	{
		static ImMenuManager *instance = new ImMenuManager();
		return *instance;
	}

	void ImMenuManager::Register(ImMenu *imMenu_)
	{
		menus.push_back(imMenu_);
		auto order = [](const ImMenu* menu)
		{
			if (menu->properties.label == "File")
				return 0;
			if (menu->properties.label == "Run")
				return 1;
			if (menu->properties.label == "Window")
				return 2;
			return 3;
		};
		std::stable_sort(menus.begin(), menus.end(), [&order](const ImMenu* left, const ImMenu* right)
		{
			return order(left) < order(right);
		});
		menusMap[imMenu_->properties.label] = imMenu_;
	}

	void ImMenuManager::UpdateAllMenus()
	{
		if (ImGui::BeginMenuBar())
		{
			for (auto menu : menus)
			{
				menu->Render();
			}
			ImGui::EndMenuBar();
		}
	}

	void ImMenuManager::SetMenuEnable(const std::string &menuName, bool enabled)
	{
		auto it = menusMap.find(menuName);
		if (it != menusMap.end())
		{
			it->second->properties.isEnabled = enabled;
		}
	}
}