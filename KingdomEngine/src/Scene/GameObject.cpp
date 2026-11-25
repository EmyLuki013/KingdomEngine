#include "KingdomEngine/Scene/GameObject.h"

#include "KingdomEngine/Scene/Scene.h"

namespace KE
{
	GameObject::GameObject(Scene* owner, ObjectId objectId, const std::string& objectName)
		: owner(owner)
		, id(objectId)
		, name(objectName)
		, active(true)
		, parent(nullptr)
	{
		components.emplace_back(new Transform(this));
	}

	void GameObject::SetName(const std::string& value)
	{
		if (name == value)
			return;

		name = value;
		MarkDirty();
	}

	void GameObject::SetActive(bool value)
	{
		if (active == value)
			return;

		active = value;
		MarkDirty();
	}

	void GameObject::MarkDirty()
	{
		owner->MarkDirty();
	}

	void GameObject::Render(const glm::mat4& viewProjection, const glm::mat4& parentTransform)
	{
		if (!active)
			return;

		const Transform* transform = GetComponent<Transform>();
		const glm::mat4 localTransform = transform && transform->IsEnabled()
			? transform->GetLocalMatrix()
			: glm::mat4(1.0f);
		const glm::mat4 worldTransform = parentTransform * localTransform;

		for (const auto& component : components)
		{
			if (component->IsEnabled())
				component->Render(viewProjection, worldTransform);
		}

		for (const auto& child : children)
			child->Render(viewProjection, worldTransform);
	}
}
