#pragma once

#include <glm/glm.hpp>

namespace KE
{
	class GameObject;

	class Component
	{
	public:
		explicit Component(GameObject* gameObject);
		virtual ~Component() = default;

		GameObject* GetGameObject() const { return gameObject; }
		bool IsEnabled() const { return enabled; }
		void SetEnabled(bool value);
		virtual void Render(const glm::mat4& viewProjection, const glm::mat4& worldTransform);

	protected:
		void MarkDirty() const;

	private:
		GameObject* gameObject;
		bool enabled;
	};
}
