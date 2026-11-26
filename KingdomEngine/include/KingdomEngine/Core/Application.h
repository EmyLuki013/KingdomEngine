#ifndef APPLICATION_H
#define APPLICATION_H

#include "KingdomEngine/Core/Core.h"
#include "KingdomEngine/Core/Log.h"
#include "kingdomEngine/Core/Window.h"
#include "KingdomEngine/ImGui/ImGuiManager.h"
#include "KingdomEngine/Renderer/OpenGL/Sprite.h"
#include "KingdomEngine/Scene/SceneManager.h"

#include "KingdomEngine/Events/ApplicationEvent.h"
#include "KingdomEngine/Events/ImGuiEvent.h"

#include <filesystem>
#include <memory>
#include <string>

namespace KE
{
	class Application
	{
	public:
		Application(bool isGameApplication);
		virtual ~Application();

		virtual void OnEvent(EventType evType) = 0;
		virtual void OnReady() = 0;
		virtual void OnUpdate() = 0;
		#ifndef GAME_APPLICATION
		virtual void OnImGuiRender() = 0;
		virtual void OnMenuBarRender() = 0;
		virtual void OnDockspaceUpdate() = 0;
		#endif

		void ActivateImGui();
		void DisableImGui();
		void CreateMainWindow();
		void DisableApplication();
		void EventHandle();

		static Application* GetInstance();
		Window& GetWindow() { return window; }
		SceneManager& GetSceneManager() { return sceneManager; }
		const SceneManager& GetSceneManager() const { return sceneManager; }
		void SetSceneViewportSize(float width, float height);
		bool StartRuntimeScene(const std::filesystem::path& scenePath,
			const std::filesystem::path& assetRoot, std::string& error);
		void StopRuntimeScene();
		bool IsRuntimeSceneRunning() const { return runtimeScene != nullptr; }

		void Run();
		void Quit();

	private:
		static Application* instance;
		bool isRunning;
		bool isGameApplication;
		ApplicationEvent appEvent;
		Window window;
		ImGuiManager& imguiManager;
		ImGuiEvent imguiEvent;
		Sprite sprite;
		SceneManager sceneManager;
		std::unique_ptr<Scene> runtimeScene;
		float sceneViewportWidth = 1.0f;
		float sceneViewportHeight = 1.0f;
	};

	Application* CreateApplication();
}

#endif
