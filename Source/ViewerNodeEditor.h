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
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    struct Geometry {
        GLuint vbo = 0;
        GLuint ebo = 0;
        int numIndices = 0;
    };

    void setSnapshot(std::shared_ptr<const TextureFrameSnapshot> snapshot);

    std::function<void()> onCompileRequested;
    std::function<void()> onSaveRequested;

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
    juce::TextButton compileButton{"Compile Graph"};
    juce::TextButton saveButton{"Save to Project"};
    
    juce::OpenGLContext openGLContext;

    std::atomic<std::shared_ptr<const TextureFrameSnapshot>> publishedSnapshot;
    std::unique_ptr<juce::OpenGLShaderProgram> shaderProgram;
    std::string currentShaderCode;
    
    float rotationX = 0.0f;
    float rotationY = 0.0f;
    juce::Point<float> lastMousePos;
    float cameraDistance = 2.5f;
    bool isRightMouseDragging = false;
    std::unordered_map<std::string, std::unique_ptr<juce::OpenGLTexture>> loadedTextures;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewerNodeEditor)
};
