#include "KingdomEngine/Scene/Transform.h"

#include <glm/gtc/matrix_transform.hpp>

namespace KE
{
	glm::mat4 Transform::GetLocalMatrix() const
	{
		glm::mat4 matrix = glm::translate(glm::mat4(1.0f), position);
		matrix = glm::rotate(matrix, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		matrix = glm::rotate(matrix, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		matrix = glm::rotate(matrix, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		return glm::scale(matrix, scale);
	}
}
