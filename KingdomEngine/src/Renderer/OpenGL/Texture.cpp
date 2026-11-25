#include "KingdomEngine/Renderer/OpenGL/Texture.h"
#include "KingdomEngine/Core/Log.h"

namespace KE
{
	Texture::Texture()
		: type(""), ID(0), unit(0), format(GL_RGBA), width(0), height(0)
	{

	}

	bool Texture::LoadTexture(const std::string& imagePath, const std::string& textureType, GLenum slot, GLenum pixelType)
	{
        //LOAD IMAGE
        type = textureType;
		int widthImg = 0;
		int heightImg = 0;
		stbi_set_flip_vertically_on_load(true);
		unsigned char* bytes = stbi_load(imagePath.c_str(), &widthImg, &heightImg, nullptr, STBI_rgb_alpha);

		//CHECK FOR ERRORS
		if (bytes)
		{
			LOG_INFO("Loaded texture file: " + imagePath);
		}
		else
		{
			LOG_ERROR("Failed to load texture: " + imagePath);
			LOG_ERROR("ERROR: " + std::string(stbi_failure_reason()));
			return false;
		}

		if (widthImg <= 0 || heightImg <= 0)
		{
			stbi_image_free(bytes);
			LOG_ERROR("Texture has invalid dimensions: " + imagePath);
			return false;
		}

		//GENERATE TEXTURE AND DEFINES FILTERS
		if (ID != 0)
			glDeleteTextures(1, &ID);
		glGenTextures(1, &ID);
		glActiveTexture(GL_TEXTURE0 + slot);
		unit = slot;
		glBindTexture(GL_TEXTURE_2D, ID);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		format = GL_RGBA;

		//CREATE TEXTURE AND GENERATE MIPMAP
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, widthImg, heightImg, 0, format, pixelType, bytes);
		glGenerateMipmap(GL_TEXTURE_2D);
		width = widthImg;
		height = heightImg;

		stbi_image_free(bytes);
		glBindTexture(GL_TEXTURE_2D, 0);
		return ID != 0;
	}

	void Texture::TextureUnit(Shader& shader, const std::string& uniform, GLuint unit)
	{
		GLuint textureUniform = glGetUniformLocation(shader.GetID(), uniform.c_str());
		shader.Activate();
		glUniform1i(textureUniform, unit);
	}

	void Texture::Bind()
	{
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, ID);
	}

	void Texture::Unbind()
	{
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	void Texture::Delete()
	{
		glDeleteTextures(1, &ID);
		ID = 0;
		width = 0;
		height = 0;
	}
}
