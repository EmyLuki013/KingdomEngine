#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "KingdomEngine/Scene/Transform.h"

namespace KE
{
	class Scene;

	using ObjectId = std::uint64_t;

	class GameObject
	{
	public:
		GameObject(const GameObject&) = delete;
		GameObject& operator=(const GameObject&) = delete;

		ObjectId GetId() const { return id; }
		const std::string& GetName() const { return name; }
		void SetName(const std::string& value);
		bool IsActive() const { return active; }
		void SetActive(bool value);

		Transform& GetTransform() { return transform; }
		const Transform& GetTransform() const { return transform; }
		GameObject* GetParent() const { return parent; }
		const std::vector<std::unique_ptr<GameObject>>& GetChildren() const { return children; }

	private:
		friend class Scene;

		GameObject(Scene* owner, ObjectId objectId, const std::string& objectName);

		Scene* owner;
		ObjectId id;
		std::string name;
		bool active;
		Transform transform;
		GameObject* parent;
		std::vector<std::unique_ptr<GameObject>> children;
	};
}
