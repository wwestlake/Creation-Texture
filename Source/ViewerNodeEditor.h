#pragma once

#include <JuceHeader.h>
#include <juce_opengl/juce_opengl.h>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include "TextureFrameSnapshot.h"



class ViewerNodeEditor : public juce::Component, private juce::OpenGLRenderer
{
public:
    ViewerNodeEditor();
    ~ViewerNodeEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    struct Geometry {
        GLuint vbo = 0;
        GLuint ebo = 0;
        int numIndices = 0;
    };

    void setSnapshot(std::shared_ptr<const TextureFrameSnapshot> snapshot);

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    
    void createGeometries();
    void compileShaderProgram(const std::string& evaluateTextureFunc);

    Geometry plane, cube, sphere, cylinder, cone, djehuti;
    
    void buildPlane();
    void buildCube();
    void buildSphere(int segments);
    void buildCylinder(int segments);
    void buildCone(int segments);
    void buildDjehuti();

    juce::ComboBox viewModeSelector;
    juce::OpenGLContext openGLContext;

    std::atomic<std::shared_ptr<const TextureFrameSnapshot>> publishedSnapshot;
    std::unique_ptr<juce::OpenGLShaderProgram> shaderProgram;
    std::string currentShaderCode;
    
    float rotationAngle = 0.0f;
    std::unordered_map<std::string, std::unique_ptr<juce::OpenGLTexture>> loadedTextures;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewerNodeEditor)
};
