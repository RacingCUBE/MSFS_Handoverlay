#include "ShaderProgram.h"
#include <fstream>
#include <sstream>
#include <iostream>

ShaderProgram::ShaderProgram() {
}

ShaderProgram::~ShaderProgram() {
    if (m_programID != 0) {
        glDeleteProgram(m_programID);
    }
}

bool ShaderProgram::loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vertexSource = readFile(vertexPath);
    std::string fragmentSource = readFile(fragmentPath);

    if (vertexSource.empty() || fragmentSource.empty()) {
        std::cerr << "Failed to read shader files" << std::endl;
        return false;
    }

    return loadFromSource(vertexSource, fragmentSource);
}

bool ShaderProgram::loadFromSource(const std::string& vertexSource, const std::string& fragmentSource) {
    // Create shaders
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

    // Compile vertex shader
    if (!compileShader(vertexShader, vertexSource)) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    // Compile fragment shader
    if (!compileShader(fragmentShader, fragmentSource)) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    // Create program
    m_programID = glCreateProgram();
    glAttachShader(m_programID, vertexShader);
    glAttachShader(m_programID, fragmentShader);

    // Link program
    bool linkSuccess = linkProgram();

    // Clean up shaders (no longer needed after linking)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return linkSuccess;
}

bool ShaderProgram::compileShader(GLuint shader, const std::string& source) {
    const char* sourceCStr = source.c_str();
    glShaderSource(shader, 1, &sourceCStr, nullptr);
    glCompileShader(shader);

    // Check compilation status
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation failed:\n" << infoLog << std::endl;
        return false;
    }

    return true;
}

bool ShaderProgram::linkProgram() {
    glLinkProgram(m_programID);

    // Check linking status
    GLint success;
    glGetProgramiv(m_programID, GL_LINK_STATUS, &success);

    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(m_programID, 512, nullptr, infoLog);
        std::cerr << "Shader program linking failed:\n" << infoLog << std::endl;
        return false;
    }

    return true;
}

std::string ShaderProgram::readFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << filepath << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void ShaderProgram::use() {
    glUseProgram(m_programID);
}

void ShaderProgram::unuse() {
    glUseProgram(0);
}

void ShaderProgram::cleanup() {
    if (m_programID != 0) {
        glDeleteProgram(m_programID);
        m_programID = 0;
    }
}

GLint ShaderProgram::getUniformLocation(const std::string& name) {
    return glGetUniformLocation(m_programID, name.c_str());
}

void ShaderProgram::setUniform1i(const std::string& name, int value) {
    glUniform1i(getUniformLocation(name), value);
}

void ShaderProgram::setUniform1f(const std::string& name, float value) {
    glUniform1f(getUniformLocation(name), value);
}

void ShaderProgram::setUniform2f(const std::string& name, float v1, float v2) {
    glUniform2f(getUniformLocation(name), v1, v2);
}

void ShaderProgram::setUniform3f(const std::string& name, float v1, float v2, float v3) {
    glUniform3f(getUniformLocation(name), v1, v2, v3);
}

void ShaderProgram::setUniform4f(const std::string& name, float v1, float v2, float v3, float v4) {
    glUniform4f(getUniformLocation(name), v1, v2, v3, v4);
}
