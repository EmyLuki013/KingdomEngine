#include "KingdomEngine/Core/Application.h"
#include "KingdomEngine/ImGui/ImWindowManager.h"

#include <algorithm>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

namespace KE
{
	Application* Application::instance = nullptr;

	Application::Application(bool isGameApplication)
		: imguiManager(ImGuiManager::Get())
	{
		instance = this;
	    LOG_INFO(isGameApplication);
		isRunning = true;
		Application::isGameApplication = isGameApplication;

		REGISTER_EVENT_LISTENER(&appEvent);	
		REGISTER_EVENT_LISTENER(&imguiEvent);
		
		Renderer::Init();
		CreateMainWindow();
	}

	Application::~Application()
	{
		if (instance == this)
			instance = nullptr;
	}

	Application* Application::GetInstance()
	{
		return instance;
	}

	void Application::SetSceneViewportSize(float width, float height)
	{
		if (width > 0.0f && height > 0.0f)
		{
			sceneViewportWidth = width;
			sceneViewportHeight = height;
		}
	}

	bool Application::StartRuntimeScene(const std::filesystem::path& scenePath,
		const std::filesystem::path& assetRoot, std::string& error)
	{
		error.clear();
		if (runtimeScene)
		{
			error = "A runtime scene is already active";
			return false;
		}

		std::unique_ptr<Scene> loadedScene = Scene::LoadFromFile(scenePath, error);
		if (!loadedScene)
			return false;

		loadedScene->SetAssetRoot(assetRoot);
		runtimeScene = std::move(loadedScene);
		return true;
	}

	void Application::StopRuntimeScene()
	{
		runtimeScene.reset();
	}

	// IMGUI
	void Application::ActivateImGui()
	{
		imguiManager.Setup(window.Get(), true);
		imguiManager.Enable();
	}

	void Application::DisableImGui()
	{
		imguiManager.Disable();
	}

	void Application::CreateMainWindow()
	{
	    if (isGameApplication)
		{
			OS::SetCurrentDir(OS::GetCurrentDir() + "\\..");
		}
		else
		{
			window.properties.isMaximized = true;
		}

		if (!window.Create())
		{
			Quit();
			return;
		}
		SetSceneViewportSize(static_cast<float>(window.properties.width),
			static_cast<float>(window.properties.height));

		Renderer::SetContext(window.Get());
		
		Renderer::InitGL();
		
		Renderer::CheckOpenGLVersion(window.Get());

        Renderer::GetFramebuffer().Create(window.properties.width, window.properties.height);

        //load shaders
        if (Renderer::GetVersion().glVersion == GLVersion::OpenGL2)
        {
            Renderer::GetDefaultShader().LoadShaders(Core::SHADERS_GL2 + "\\sprite.vert", Core::SHADERS_GL2 + "\\sprite.frag");
        }
        else if (Renderer::GetVersion().glVersion == GLVersion::OpenGL3)
        {
            Renderer::GetDefaultShader().LoadShaders(Core::SHADERS_GL3 + "\\sprite.vert", Core::SHADERS_GL3 + "\\sprite.frag");
        }

        sprite.LoadTexture(Core::RESOURCES_DIR + "\\empty.png");
	}

	void Application::DisableApplication()
	{
	    sprite.Delete();
		DisableImGui();
		Renderer::GetFramebuffer().Delete();
		window.Destroy();
	}

	void Application::EventHandle()
	{
		OnEvent(appEvent.eventType);
		
		switch (appEvent.eventType)
		{
		case EventType::LoadGL3:
			DisableApplication();
			Renderer::SetGLVersion(OpenGL3);
			CreateMainWindow();
			if (window.Get() != nullptr)
			{
				ActivateImGui();
				ImWindowManager::Get().OnReadyAllWindows();
			}
			appEvent.eventType = EventType::None;
			break;
		case EventType::LoadGL2:
			DisableApplication();
			Renderer::SetGLVersion(OpenGL2);
			CreateMainWindow();
			if (window.Get() != nullptr)
			{
				ActivateImGui();
				ImWindowManager::Get().OnReadyAllWindows();
			}
			appEvent.eventType = EventType::None;
			break;
        case EventType::WindowClose:
			if (!imguiManager.IsEnabled())
                Quit();
			appEvent.eventType = EventType::None;
			break;
		default:
			break;
		}
	}

	void Application::Run()
	{
		OnReady();

		auto previousFrameTime = std::chrono::steady_clock::now();
		while (isRunning)
		{
			const auto currentFrameTime = std::chrono::steady_clock::now();
			const float deltaTime = std::min(
				std::chrono::duration<float>(currentFrameTime - previousFrameTime).count(), 0.1f);
			previousFrameTime = currentFrameTime;

            imguiManager.CreateNewFrame();

			Renderer::GetFramebuffer().Bind();
			Renderer::Clear();
			OnUpdate();
			Scene* scene = runtimeScene ? runtimeScene.get() : sceneManager.GetActiveScene();
			if (scene)
			{
				if (isGameApplication || runtimeScene)
					scene->Update(deltaTime);

				constexpr float viewHeight = 10.0f;
				const float aspect = sceneViewportWidth / sceneViewportHeight;
				const float halfHeight = viewHeight * 0.5f;
				const float halfWidth = halfHeight * aspect;
				const glm::mat4 viewProjection = glm::ortho(
					-halfWidth, halfWidth, -halfHeight, halfHeight, -100.0f, 100.0f);
				scene->Render(viewProjection);
			}
			Renderer::GetFramebuffer().Unbind();

			// IMGUI DOCKSPACE
			imguiManager.BeginDockspace();
			if (imguiManager.IsEnabled())
			{
				OnDockspaceUpdate();
				OnMenuBarRender();
			}
			imguiManager.EndDockspace();

			// render client-application imgui
			if (imguiManager.IsEnabled())
				OnImGuiRender();

            imguiManager.Render();

            if (!imguiManager.IsEnabled())
			{
				Renderer::GetFramebuffer().Draw(window.properties.width, window.properties.height);
				sprite.Draw();
			}

			window.Update();
			
			PROCESS_ALL_EVENTS();
			EventHandle();
		}
	}

	void Application::Quit()
	{
		isRunning = false;
	}
}
