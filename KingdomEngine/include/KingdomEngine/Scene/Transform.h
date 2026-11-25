#pragma once

#include "KingdomEngine/Scene/Component.h"

namespace KE
{
	class Transform : public Component
	{
	public:
		explicit Transform(GameObject* gameObject = nullptr)
			: Component(gameObject)
		{
		}

		glm::vec3 position{ 0.0f };
		glm::vec3 rotation{ 0.0f };
		glm::vec3 scale{ 1.0f };

		glm::mat4 GetLocalMatrix() const;
	};
}
