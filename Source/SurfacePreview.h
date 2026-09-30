#pragma once

#include <JuceHeader.h>
#include <juce_opengl/juce_opengl.h>

#include <mutex>

// Surface Map's lit 3D preview: a sphere, cube or plane with the maps applied - base colour, tangent-space normal
// map, occlusion and roughness (from the packed ORM image). Right-drag rotates the object, left-drag moves the
// light, the wheel zooms. Unlike the Materials viewer it has tangents, so normal maps render properly.
class SurfacePreview final : public juce::Component, private juce::OpenGLRenderer
{
public:
    SurfacePreview();
    ~SurfacePreview() override;

    // Message thread. baseColour is sRGB; normal and orm hold raw data values. Empty images clear the preview.
    void setMaps(const juce::Image& baseColour, const juce::Image& normal, const juce::Image& orm, bool directXNormals);

    void paint(juce::Graphics&) override {}
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    struct Mesh
    {
        GLuint vbo = 0, ebo = 0;
        int indexCount = 0;
    };

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void buildMeshes();
    void upload(Mesh& mesh, const std::vector<float>& vertices, const std::vector<unsigned int>& indices);

    juce::OpenGLContext context;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    GLuint vertexArray = 0;
    Mesh sphere, cube, plane;
    juce::OpenGLTexture baseTexture, normalTexture, ormTexture;

    std::mutex pendingLock;
    juce::Image pendingBase, pendingNormal, pendingOrm;
    bool pendingChanged = false;
    bool haveMaps = false;
    std::atomic<bool> directX { false };

    juce::ComboBox shapeBox;
    std::atomic<int> shape { 1 };
    std::atomic<float> rotationX { 0.35f }, rotationY { 0.6f }, lightYaw { 0.8f }, lightPitch { 0.7f }, distance { 2.6f };
    juce::Point<float> lastDrag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SurfacePreview)
};
