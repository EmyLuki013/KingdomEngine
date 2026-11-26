#include "KingdomEngine/Scene/SpriteComponent.h"

#include "KingdomEngine/Scene/GameObject.h"
#include "KingdomEngine/Scene/Scene.h"
#include "KingdomEngine/Renderer/OpenGL/Sprite.h"

#include <filesystem>

namespace KE
{
	SpriteComponent::SpriteComponent(GameObject* gameObject)
		: Component(gameObject)
		, textureLoadAttempted(false)
	{
	}

	SpriteComponent::~SpriteComponent() = default;

	void SpriteComponent::SetTexturePath(const std::string& value)
	{
		const bool changed = texturePath != value;
		sprite.reset();
		texturePath = value;
		textureLoadAttempted = false;
		if (changed)
			MarkDirty();
	}

	void SpriteComponent::Render(const glm::mat4& viewProjection, const glm::mat4& worldTransform)
	{
		if (texturePath.empty())
			return;

		if (!sprite)
			sprite.reset(new Sprite());
		if (!textureLoadAttempted)
		{
			textureLoadAttempted = true;
			std::filesystem::path resolvedPath(texturePath);
			if (resolvedPath.is_relative())
			{
				const GameObject* gameObject = GetGameObject();
				const Scene* scene = gameObject ? gameObject->GetScene() : nullptr;
				if (scene && !scene->GetAssetRoot().empty())
					resolvedPath = scene->GetAssetRoot() / resolvedPath;
			}

			if (!sprite->LoadTexture(resolvedPath.string()))
				return;
		}
		if (sprite->GetTextureID() == 0)
			return;

		sprite->Draw(viewProjection * worldTransform);
	}
}
