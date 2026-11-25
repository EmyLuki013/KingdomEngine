#ifndef DETAILS_PANEL_WINDOW_H
#define DETAILS_PANEL_WINDOW_H

#include <KingdomEngine/Core/Application.h>
#include <KingdomEngine/ImGui/ImWindow.h>
#include <KingdomEngine/Scene/Scene.h>

#include "KingdomEditor/Utils/SceneEditorContext.h"

#include <cstring>

namespace Editor
{
	class DetailsPanelWindow : public KE::ImWindow
	{
	public:
		DetailsPanelWindow() : KE::ImWindow("Properties", 1184, 19, 182, 578, true, false, KE::DockSide::RIGHT)
		{

		}

		void OnReady() override
		{

		}

		void OnRender() override
		{
			KE::Application* application = KE::Application::GetInstance();
			if (!application)
			{
				ImGui::TextDisabled("Engine application is not available");
				return;
			}

			KE::Scene* scene = application->GetSceneManager().GetActiveScene();
			KE::GameObject* object = scene ? scene->FindObject(selectedSceneObjectId) : nullptr;
			static KE::ObjectId editingObjectId = 0;
			static const KE::Scene* editingScene = nullptr;
			static char nameBuffer[256] = "";
			if (!object)
			{
				selectedSceneObjectId = 0;
				editingObjectId = 0;
				editingScene = nullptr;
				ImGui::TextDisabled("Select an object to inspect its properties");
				return;
			}

			if (editingScene != scene || editingObjectId != object->GetId())
			{
				editingScene = scene;
				editingObjectId = object->GetId();
				std::strncpy(nameBuffer, object->GetName().c_str(), sizeof(nameBuffer) - 1);
				nameBuffer[sizeof(nameBuffer) - 1] = '\0';
			}

			if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
				object->SetName(nameBuffer);

			bool active = object->IsActive();
			if (ImGui::Checkbox("Active", &active))
				object->SetActive(active);

			ImGui::Separator();
			KE::Transform& transform = object->GetTransform();
			bool transformChanged = false;
			transformChanged |= ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
			transformChanged |= ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.5f);
			transformChanged |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
			if (transformChanged)
				scene->MarkDirty();
		}
	};
}

#endif
