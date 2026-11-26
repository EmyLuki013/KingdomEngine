#include "KingdomEngine/Scene/Component.h"

#include "KingdomEngine/Scene/GameObject.h"

namespace KE
{
	Component::Component(GameObject* owner)
		: gameObject(owner)
		, enabled(true)
	{
	}

	void Component::SetEnabled(bool value)
	{
		if (enabled == value)
			return;

		enabled = value;
		MarkDirty();
	}

	void Component::MarkDirty() const
	{
		if (gameObject)
			gameObject->MarkDirty();
	}

	void Component::Update(float)
	{
	}

	void Component::Render(const glm::mat4&, const glm::mat4&)
	{
	}
}
