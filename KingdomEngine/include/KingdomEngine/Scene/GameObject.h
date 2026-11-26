#pragma once

#include <cstdint>
#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "KingdomEngine/Scene/Component.h"
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
		Scene* GetScene() const { return owner; }

		Transform& GetTransform() { return *GetComponent<Transform>(); }
		const Transform& GetTransform() const { return *GetComponent<Transform>(); }
		GameObject* GetParent() const { return parent; }
		const std::vector<std::unique_ptr<GameObject>>& GetChildren() const { return children; }
		const std::vector<std::unique_ptr<Component>>& GetComponents() const { return components; }

		void Update(float deltaTime);

		template<typename T, typename... Args>
		T* AddComponent(Args&&... args)
		{
			static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
			if (T* existing = GetComponent<T>())
				return existing;

			std::unique_ptr<T> component = std::make_unique<T>(this, std::forward<Args>(args)...);
			T* result = component.get();
			components.push_back(std::move(component));
			MarkDirty();
			return result;
		}

		template<typename T>
		T* GetComponent()
		{
			for (const auto& component : components)
			{
				if (T* match = dynamic_cast<T*>(component.get()))
					return match;
			}
			return nullptr;
		}

		template<typename T>
		const T* GetComponent() const
		{
			for (const auto& component : components)
			{
				if (const T* match = dynamic_cast<const T*>(component.get()))
					return match;
			}
			return nullptr;
		}

		template<typename T>
		bool RemoveComponent()
		{
			static_assert(!std::is_same<T, Transform>::value, "GameObject always requires a Transform");
			auto it = std::find_if(components.begin(), components.end(), [](const std::unique_ptr<Component>& component)
			{
				return dynamic_cast<T*>(component.get()) != nullptr;
			});
			if (it == components.end())
				return false;

			components.erase(it);
			MarkDirty();
			return true;
		}

	private:
		friend class Scene;
		friend class Component;

		GameObject(Scene* owner, ObjectId objectId, const std::string& objectName);
		void MarkDirty();
		void Render(const glm::mat4& viewProjection, const glm::mat4& parentTransform);

		Scene* owner;
		ObjectId id;
		std::string name;
		bool active;
		GameObject* parent;
		std::vector<std::unique_ptr<Component>> components;
		std::vector<std::unique_ptr<GameObject>> children;
	};
}
