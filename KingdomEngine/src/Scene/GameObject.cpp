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
	}

	void GameObject::SetName(const std::string& value)
	{
		if (name == value)
			return;

		name = value;
		owner->MarkDirty();
	}

	void GameObject::SetActive(bool value)
	{
		if (active == value)
			return;

		active = value;
		owner->MarkDirty();
	}
}
