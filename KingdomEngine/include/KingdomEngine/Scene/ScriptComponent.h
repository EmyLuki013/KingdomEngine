#pragma once

#include "KingdomEngine/Scene/Component.h"

#include <string>

struct lua_State;

namespace KE
{
	class ScriptComponent : public Component
	{
	public:
		explicit ScriptComponent(GameObject* gameObject);
		~ScriptComponent() override;

		const std::string& GetScriptPath() const { return scriptPath; }
		void SetScriptPath(const std::string& value);
		void Update(float deltaTime) override;

	private:
		bool LoadScript();
		bool CallStart();
		bool CallUpdate(float deltaTime);
		void ReportLuaError(const char* callback);
		void CloseScript();

		std::string scriptPath;
		lua_State* state;
		bool loadAttempted;
		bool startCalled;
		bool failed;
	};
}
