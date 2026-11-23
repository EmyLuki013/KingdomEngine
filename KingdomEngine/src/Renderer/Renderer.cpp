#include "KingdomEngine/Renderer/Renderer.h"
#include "KingdomEngine/Core/Log.h"
#include "KingdomEngine/Events/Event.h"

#include <iostream>
#include <ImGui/imgui.h>

namespace KE
{
	OpenGLVersion Renderer::version;
	SDL_GLContext Renderer::glContext;
	Color Renderer::clearColor;
	Framebuffer Renderer::framebuffer;
	Shader Renderer::defaultShader;

	void Renderer::Init()
	{
		// INIT SDL
		if (SDL_Init(SDL_INIT_VIDEO) < 0)
		{
			LOG_FATAL("Failed to initialize SDL");
		}
		else
		{
			LOG_INFO("Initialized SDL");
		}

		// SET GL ATTRIBUTES
		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

		if (version.glVersion == OpenGL3)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		}
		else if (version.glVersion == OpenGL2)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
		}

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
		SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
	}

	void Renderer::SetContext(SDL_Window *window)
	{
		// DEBUG: Verificar SDL video driver
		const char* driver = SDL_GetCurrentVideoDriver();
		LOG_INFO("SDL Video Driver: " + std::string(driver ? driver : "NULL"));
		
		// DEBUG: Verificar todas as configurações GL
		int major, minor, profile, accelerated;
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
		SDL_GL_GetAttribute(SDL_GL_ACCELERATED_VISUAL, &accelerated);
		
		LOG_INFO("SDL_GL Requested: " + std::to_string(major) + "." + std::to_string(minor));
		LOG_INFO("SDL_GL Profile: " + std::to_string(profile));
		LOG_INFO("SDL_GL Accelerated: " + std::to_string(accelerated));
		
		glContext = SDL_GL_CreateContext(window);
		if (!glContext) {
			LOG_FATAL("SDL_GL_CreateContext failed: " + std::string(SDL_GetError()));
			return;
		}
		
		SDL_GL_MakeCurrent(window, glContext);
		
		// DEBUG CRÍTICO: Verificar OpenGL ANTES do GLEW
		typedef const GLubyte* (APIENTRY* PFNGLGETSTRING)(GLenum);
		PFNGLGETSTRING glGetStringFunc = (PFNGLGETSTRING)SDL_GL_GetProcAddress("glGetString");
		
		if (glGetStringFunc) {
			const GLubyte* vendor = glGetStringFunc(GL_VENDOR);
			const GLubyte* renderer = glGetStringFunc(GL_RENDERER);
			const GLubyte* version = glGetStringFunc(GL_VERSION);
			
			if (vendor) LOG_INFO("GL Vendor (via SDL): " + std::string((const char*)vendor));
			if (renderer) LOG_INFO("GL Renderer (via SDL): " + std::string((const char*)renderer));
			if (version) LOG_INFO("GL Version (via SDL): " + std::string((const char*)version));
			
			// Se mostrar "Microsoft" no vendor, temos problema
			if (vendor && std::string((const char*)vendor).find("Microsoft") != std::string::npos) {
				LOG_ERROR("USING MICROSOFT SOFTWARE RENDERER! No hardware acceleration.");
			}
		}
		
		SDL_GL_SetSwapInterval(1);
	}

	void Renderer::InitGL()
	{
		// INIT GLAD
		if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
		{
			LOG_FATAL("Failed to initialize GLAD");
			return;
		}
		else
		{
			LOG_INFO("Initialized GLAD");
		}

		LOG_INFO(std::string("OpenGL Version: ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    	LOG_INFO(std::string("GLSL Version: ") + reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

        //log GL Version
		const GLubyte* version = glGetString(GL_VERSION);
		if (!version)
		{
			LOG_FATAL("Failed to get OpenGL version string");
			return;
		}
		std::string glVersionStr = "OpenGL Version: " + std::string(reinterpret_cast<const char*>(version));
        LOG_INFO(glVersionStr);

		int major, minor;
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
		SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
		LOG_INFO("SDL_GL Context requested: " + std::to_string(major) + "." + std::to_string(minor));

        //enable gl features
		glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glFrontFace(GL_CCW);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	void Renderer::CheckOpenGLVersion(SDL_Window *window)
	{
		int major = version.majorVersion;
		int minor = version.minorVersion;
		const char *version = (const char *)glGetString(GL_VERSION);

		int currentMajor, currentMinor;
		sscanf(version, "%d.%d", &currentMajor, &currentMinor);

		if (!(currentMajor > major || (currentMajor == major && currentMinor >= minor)))
		{
			LOG_FATAL("Incompatible OpenGL version");
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
									 "Incompatible OpenGL Version",
									 "Your system does not support the required OpenGL version. Please update your drivers or use a compatible system.",
									 window);
			DISPATCH_EVENT(EventType::LoadGL2);
		}
	}

	void Renderer::Clear()
	{
		glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
	}

	void Renderer::SetGLVersion(GLVersion version_)
	{
		if (version_ == OpenGL2)
		{
			version.glVersion = OpenGL2;
			version.minorVersion = 1;
			version.majorVersion = 2;
		}
		else if (version_ == OpenGL3)
		{
			version.glVersion = OpenGL3;
			version.minorVersion = 3;
			version.majorVersion = 3;
		}
	}
}
