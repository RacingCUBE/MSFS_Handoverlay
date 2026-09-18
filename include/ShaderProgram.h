#pragma once

#include <GL/glew.h>
#include <string>

class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();

    bool loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath);
    bool loadFromSource(const std::string& vertexSource, const std::string& fragmentSource);

    void use();
    void unuse();
    void cleanup();  // Explicit cleanup (also handled by destructor)

    GLuint getProgramID() const { return m_programID; }

    // Uniform setters
    void setUniform1i(const std::string& name, int value);
    void setUniform1f(const std::string& name, float value);
    void setUniform2f(const std::string& name, float v1, float v2);
    void setUniform3f(const std::string& name, float v1, float v2, float v3);
    void setUniform4f(const std::string& name, float v1, float v2, float v3, float v4);

private:
    GLuint m_programID = 0;

    bool compileShader(GLuint shader, const std::string& source);
    bool linkProgram();
    std::string readFile(const std::string& filepath);
    GLint getUniformLocation(const std::string& name);
};
