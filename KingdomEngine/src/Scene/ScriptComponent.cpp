#include "KingdomEngine/Scene/ScriptComponent.h"

#include "KingdomEngine/Core/Log.h"
#include "KingdomEngine/Scene/GameObject.h"
#include "KingdomEngine/Scene/Scene.h"
#include "KingdomEngine/Scene/Transform.h"

#include <filesystem>

#include <lua.hpp>

namespace KE
{
	namespace
	{
		const char* GameObjectMetatable = "KingdomEngine.GameObject";

		struct LuaGameObjectHandle
		{
			Scene* scene;
			ObjectId id;
		};

		LuaGameObjectHandle* CheckGameObjectHandle(lua_State* state)
		{
			return static_cast<LuaGameObjectHandle*>(luaL_checkudata(state, 1, GameObjectMetatable));
		}

		GameObject* ResolveGameObject(lua_State* state, LuaGameObjectHandle* handle)
		{
			GameObject* object = handle->scene ? handle->scene->FindObject(handle->id) : nullptr;
			if (!object)
				luaL_error(state, "GameObject no longer exists");
			return object;
		}

		void PushGameObjectHandle(lua_State* state, Scene* scene, ObjectId id)
		{
			auto* handle = static_cast<LuaGameObjectHandle*>(lua_newuserdatauv(state, sizeof(LuaGameObjectHandle), 0));
			handle->scene = scene;
			handle->id = id;
			luaL_setmetatable(state, GameObjectMetatable);
		}

		int LuaGetName(lua_State* state)
		{
			GameObject* object = ResolveGameObject(state, CheckGameObjectHandle(state));
			const std::string& name = object->GetName();
			lua_pushlstring(state, name.data(), name.size());
			return 1;
		}

		int LuaGetPosition(lua_State* state)
		{
			GameObject* object = ResolveGameObject(state, CheckGameObjectHandle(state));
			const glm::vec3& position = object->GetTransform().position;
			lua_pushnumber(state, position.x);
			lua_pushnumber(state, position.y);
			lua_pushnumber(state, position.z);
			return 3;
		}

		int LuaSetPosition(lua_State* state)
		{
			GameObject* object = ResolveGameObject(state, CheckGameObjectHandle(state));
			object->GetTransform().position = glm::vec3(
				static_cast<float>(luaL_checknumber(state, 2)),
				static_cast<float>(luaL_checknumber(state, 3)),
				static_cast<float>(luaL_checknumber(state, 4)));
			object->GetScene()->MarkDirty();
			return 0;
		}

		int LuaGetRotation(lua_State* state)
		{
			GameObject* object = ResolveGameObject(state, CheckGameObjectHandle(state));
			const glm::vec3& rotation = object->GetTransform().rotation;
			lua_pushnumber(state, rotation.x);
			lua_pushnumber(state, rotation.y);
			lua_pushnumber(state, rotation.z);
			return 3;
		}

		int LuaSetRotation(lua_State* state)
		{
			GameObject* object = ResolveGameObject(state, CheckGameObjectHandle(state));
			object->GetTransform().rotation = glm::vec3(
				static_cast<float>(luaL_checknumber(state, 2)),
				static_cast<float>(luaL_checknumber(state, 3)),
				static_cast<float>(luaL_checknumber(state, 4)));
			object->GetScene()->MarkDirty();
			return 0;
		}

		int LuaGetScale(lua_State* state)
		{
			GameObject* object = ResolveGameObject(state, CheckGameObjectHandle(state));
			const glm::vec3& scale = object->GetTransform().scale;
			lua_pushnumber(state, scale.x);
			lua_pushnumber(state, scale.y);
			lua_pushnumber(state, scale.z);
			return 3;
		}

		int LuaSetScale(lua_State* state)
		{
			GameObject* object = ResolveGameObject(state, CheckGameObjectHandle(state));
			object->GetTransform().scale = glm::vec3(
				static_cast<float>(luaL_checknumber(state, 2)),
				static_cast<float>(luaL_checknumber(state, 3)),
				static_cast<float>(luaL_checknumber(state, 4)));
			object->GetScene()->MarkDirty();
			return 0;
		}

		int LuaSetActive(lua_State* state)
		{
			GameObject* object = ResolveGameObject(state, CheckGameObjectHandle(state));
			object->SetActive(lua_toboolean(state, 2) != 0);
			return 0;
		}

		int LuaFindObject(lua_State* state)
		{
			LuaGameObjectHandle* handle = CheckGameObjectHandle(state);
			ResolveGameObject(state, handle);
			size_t nameLength = 0;
			const char* name = luaL_checklstring(state, 2, &nameLength);
			GameObject* object = handle->scene->FindObjectByName(std::string(name, nameLength));
			if (!object)
			{
				lua_pushnil(state);
				return 1;
			}

			PushGameObjectHandle(state, handle->scene, object->GetId());
			return 1;
		}

		void RegisterGameObjectApi(lua_State* state)
		{
			if (luaL_newmetatable(state, GameObjectMetatable))
			{
				lua_pushvalue(state, -1);
				lua_setfield(state, -2, "__index");
				const luaL_Reg methods[] = {
					{ "get_name", LuaGetName },
					{ "get_position", LuaGetPosition },
					{ "set_position", LuaSetPosition },
					{ "get_rotation", LuaGetRotation },
					{ "set_rotation", LuaSetRotation },
					{ "get_scale", LuaGetScale },
					{ "set_scale", LuaSetScale },
					{ "set_active", LuaSetActive },
					{ "find_object", LuaFindObject },
					{ nullptr, nullptr }
				};
				luaL_setfuncs(state, methods, 0);
			}
			lua_pop(state, 1);
		}
	}

	ScriptComponent::ScriptComponent(GameObject* gameObject)
		: Component(gameObject)
		, state(nullptr)
		, loadAttempted(false)
		, startCalled(false)
		, failed(false)
	{
	}

	ScriptComponent::~ScriptComponent()
	{
		CloseScript();
	}

	void ScriptComponent::SetScriptPath(const std::string& value)
	{
		if (scriptPath == value)
			return;

		CloseScript();
		scriptPath = value;
		loadAttempted = false;
		startCalled = false;
		failed = false;
		MarkDirty();
	}

	void ScriptComponent::Update(float deltaTime)
	{
		if (failed || scriptPath.empty())
			return;

		if (!loadAttempted)
		{
			loadAttempted = true;
			if (!LoadScript())
			{
				failed = true;
				return;
			}
		}

		if (!startCalled)
		{
			startCalled = true;
			if (!CallStart())
			{
				failed = true;
				return;
			}
		}

		if (!CallUpdate(deltaTime))
			failed = true;
	}

	bool ScriptComponent::LoadScript()
	{
		state = luaL_newstate();
		if (!state)
		{
			LOG_ERROR("Could not create Lua state for script: " + scriptPath);
			return false;
		}

		luaL_openlibs(state);
		RegisterGameObjectApi(state);

		std::filesystem::path resolvedPath(scriptPath);
		if (resolvedPath.is_relative())
		{
			const GameObject* gameObject = GetGameObject();
			const Scene* scene = gameObject ? gameObject->GetScene() : nullptr;
			if (scene && !scene->GetAssetRoot().empty())
				resolvedPath = scene->GetAssetRoot() / resolvedPath;
		}

		if (luaL_loadfile(state, resolvedPath.string().c_str()) != LUA_OK ||
			lua_pcall(state, 0, 0, 0) != LUA_OK)
		{
			ReportLuaError("load");
			CloseScript();
			return false;
		}
		return true;
	}

	bool ScriptComponent::CallStart()
	{
		lua_getglobal(state, "on_start");
		if (!lua_isfunction(state, -1))
		{
			lua_pop(state, 1);
			return true;
		}

		const GameObject* gameObject = GetGameObject();
		PushGameObjectHandle(state, gameObject->GetScene(), gameObject->GetId());
		if (lua_pcall(state, 1, 0, 0) != LUA_OK)
		{
			ReportLuaError("on_start");
			return false;
		}
		return true;
	}

	bool ScriptComponent::CallUpdate(float deltaTime)
	{
		lua_getglobal(state, "on_update");
		if (!lua_isfunction(state, -1))
		{
			lua_pop(state, 1);
			return true;
		}

		const GameObject* gameObject = GetGameObject();
		PushGameObjectHandle(state, gameObject->GetScene(), gameObject->GetId());
		lua_pushnumber(state, deltaTime);
		if (lua_pcall(state, 2, 0, 0) != LUA_OK)
		{
			ReportLuaError("on_update");
			return false;
		}
		return true;
	}

	void ScriptComponent::ReportLuaError(const char* callback)
	{
		const char* message = lua_tostring(state, -1);
		LOG_ERROR("Lua script '" + scriptPath + "' " + callback + " error: " +
			(message ? message : "unknown error"));
		lua_pop(state, 1);
	}

	void ScriptComponent::CloseScript()
	{
		if (state)
		{
			lua_close(state);
			state = nullptr;
		}
	}
}
