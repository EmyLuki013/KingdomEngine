#pragma once

#include "KingdomEngine/Scene/Component.h"

#include <memory>
#include <string>

namespace KE
{
	class Sprite;

	class SpriteComponent : public Component
	{
	public:
		explicit SpriteComponent(GameObject* gameObject);
		~SpriteComponent() override;

		const std::string& GetTexturePath() const { return texturePath; }
		void SetTexturePath(const std::string& value);
		void Render(const glm::mat4& viewProjection, const glm::mat4& worldTransform) override;

	private:
		std::string texturePath;
		std::unique_ptr<Sprite> sprite;
		bool textureLoadAttempted;
	};
}
