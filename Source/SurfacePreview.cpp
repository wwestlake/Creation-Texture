#include "SurfacePreview.h"

#include <cmath>

using namespace juce::gl;

namespace
{
// Vertex layout: position 3, normal 3, uv 2, tangent 3.
constexpr int stride = 11;

const char* vertexShader = R"(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec3 aNormal;
    layout(location = 2) in vec2 aUV;
    layout(location = 3) in vec3 aTangent;
    uniform mat4 projection;
    uniform mat4 view;
    uniform mat4 model;
    out vec2 vUV;
    out vec3 vNormal;
    out vec3 vTangent;
    out vec3 vWorldPos;
    void main() {
        vUV = aUV;
        vNormal = mat3(model) * aNormal;
        vTangent = mat3(model) * aTangent;
        vec4 world = model * vec4(aPos, 1.0);
        vWorldPos = world.xyz;
        gl_Position = projection * view * world;
    }
)";

const char* fragmentShader = R"(
    #version 330 core
    in vec2 vUV;
    in vec3 vNormal;
    in vec3 vTangent;
    in vec3 vWorldPos;
    out vec4 FragColor;
    uniform sampler2D baseMap;
    uniform sampler2D normalMap;
    uniform sampler2D ormMap;
    uniform int hasMaps;
    uniform int directX;
    uniform vec3 lightDir;
    uniform vec3 cameraPos;
    void main() {
        vec3 N = normalize(vNormal);
        vec3 T = normalize(vTangent - N * dot(N, vTangent));
        vec3 B = cross(N, T);
        vec3 base = vec3(0.6);
        float ao = 1.0;
        float roughness = 0.6;
        if (hasMaps == 1) {
            base = pow(texture(baseMap, vUV).rgb, vec3(2.2));          // sRGB -> linear
            vec3 n = texture(normalMap, vUV).rgb * 2.0 - 1.0;
            if (directX == 1) n.y = -n.y;
            N = normalize(T * n.x + B * n.y + N * n.z);
            vec3 orm = texture(ormMap, vUV).rgb;
            ao = orm.r;
            roughness = clamp(orm.g, 0.04, 1.0);
        }
        vec3 L = normalize(lightDir);
        vec3 V = normalize(cameraPos - vWorldPos);
        vec3 H = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float alpha = roughness * roughness;
        float d = NdotH * NdotH * (alpha * alpha - 1.0) + 1.0;
        float D = alpha * alpha / (3.14159 * d * d);
        vec3 specular = vec3(0.04) * D * NdotL;
        vec3 colour = base * (0.12 * ao + NdotL * 0.9) + specular;
        FragColor = vec4(pow(colour, vec3(1.0 / 2.2)), 1.0);
    }
)";

juce::Matrix3D<float> rotationXMatrix(float a)
{
    juce::Matrix3D<float> m;
    m.mat[5] = std::cos(a); m.mat[6] = std::sin(a);
    m.mat[9] = -std::sin(a); m.mat[10] = std::cos(a);
    return m;
}

juce::Matrix3D<float> rotationYMatrix(float a)
{
    juce::Matrix3D<float> m;
    m.mat[0] = std::cos(a); m.mat[2] = -std::sin(a);
    m.mat[8] = std::sin(a); m.mat[10] = std::cos(a);
    return m;
}
}

SurfacePreview::SurfacePreview()
{
    shapeBox.addItem("Sphere", 1);
    shapeBox.addItem("Cube", 2);
    shapeBox.addItem("Plane", 3);
    shapeBox.setSelectedId(1, juce::dontSendNotification);
    shapeBox.onChange = [this]() { shape = shapeBox.getSelectedId(); };
    addAndMakeVisible(shapeBox);

    context.setRenderer(this);
    context.setContinuousRepainting(true);
    context.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    context.attachTo(*this);
}

SurfacePreview::~SurfacePreview()
{
    context.detach();
}

void SurfacePreview::resized()
{
    shapeBox.setBounds(getLocalBounds().removeFromTop(30).reduced(4).removeFromLeft(140));
}

void SurfacePreview::setMaps(const juce::Image& baseColour, const juce::Image& normal, const juce::Image& orm, bool directXNormals)
{
    std::lock_guard<std::mutex> lock(pendingLock);
    pendingBase = baseColour;
    pendingNormal = normal;
    pendingOrm = orm;
    pendingChanged = true;
    directX = directXNormals;
}

void SurfacePreview::mouseDown(const juce::MouseEvent& e)
{
    lastDrag = e.position;
}

void SurfacePreview::mouseDrag(const juce::MouseEvent& e)
{
    const auto delta = e.position - lastDrag;
    lastDrag = e.position;
    if (e.mods.isRightButtonDown())
    {
        rotationY = rotationY + delta.x * 0.01f;
        rotationX = rotationX + delta.y * 0.01f;
    }
    else
    {
        lightYaw = lightYaw + delta.x * 0.01f;
        lightPitch = juce::jlimit(-1.5f, 1.5f, lightPitch - delta.y * 0.01f);
    }
}

void SurfacePreview::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    distance = juce::jlimit(1.2f, 8.0f, distance - wheel.deltaY * 1.5f);
}

void SurfacePreview::upload(Mesh& mesh, const std::vector<float>& vertices, const std::vector<unsigned int>& indices)
{
    auto& ext = context.extensions;
    ext.glGenBuffers(1, &mesh.vbo);
    ext.glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    ext.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
    ext.glGenBuffers(1, &mesh.ebo);
    ext.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    ext.glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);
    mesh.indexCount = static_cast<int>(indices.size());
}

void SurfacePreview::buildMeshes()
{
    // Sphere: tangent points along increasing u (around the equator). UVs tile twice around.
    {
        std::vector<float> v;
        std::vector<unsigned int> idx;
        const int rings = 48, segments = 96;
        for (int r = 0; r <= rings; ++r)
        {
            const float vv = static_cast<float>(r) / rings;
            const float phi = vv * juce::MathConstants<float>::pi;
            for (int s = 0; s <= segments; ++s)
            {
                const float uu = static_cast<float>(s) / segments;
                const float theta = uu * juce::MathConstants<float>::twoPi;
                const float x = std::sin(phi) * std::cos(theta), y = std::cos(phi), z = std::sin(phi) * std::sin(theta);
                v.insert(v.end(), { x * 0.8f, y * 0.8f, z * 0.8f, x, y, z, uu * 2.0f, vv, -std::sin(theta), 0.0f, std::cos(theta) });
            }
        }
        for (int r = 0; r < rings; ++r)
            for (int s = 0; s < segments; ++s)
            {
                const unsigned a = static_cast<unsigned>(r * (segments + 1) + s), b = a + static_cast<unsigned>(segments + 1);
                idx.insert(idx.end(), { a, b, a + 1, a + 1, b, b + 1 });
            }
        upload(sphere, v, idx);
    }

    // Cube: each face has its own normal and tangent.
    {
        struct Face { float n[3], t[3], b[3]; };
        const Face faces[6] = {
            { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },  { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
            { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } }, { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
            { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } }, { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        };
        std::vector<float> v;
        std::vector<unsigned int> idx;
        for (int f = 0; f < 6; ++f)
        {
            const auto& face = faces[f];
            const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
            const unsigned base = static_cast<unsigned>(f * 4);
            for (const auto& c : corners)
            {
                float p[3];
                for (int k = 0; k < 3; ++k)
                    p[k] = 0.55f * (face.n[k] + c[0] * face.t[k] + c[1] * face.b[k]);
                v.insert(v.end(), { p[0], p[1], p[2], face.n[0], face.n[1], face.n[2], (c[0] + 1.0f) * 0.5f, 1.0f - (c[1] + 1.0f) * 0.5f,
                                    face.t[0], face.t[1], face.t[2] });
            }
            idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
        upload(cube, v, idx);
    }

    // Plane facing the camera.
    {
        const std::vector<float> v {
            -1, -1, 0, 0, 0, 1, 0, 1, 1, 0, 0,
             1, -1, 0, 0, 0, 1, 1, 1, 1, 0, 0,
             1,  1, 0, 0, 0, 1, 1, 0, 1, 0, 0,
            -1,  1, 0, 0, 0, 1, 0, 0, 1, 0, 0,
        };
        upload(plane, v, { 0, 1, 2, 0, 2, 3 });
    }
}

void SurfacePreview::newOpenGLContextCreated()
{
    context.extensions.glGenVertexArrays(1, &vertexArray);
    context.extensions.glBindVertexArray(vertexArray);
    buildMeshes();

    auto program = std::make_unique<juce::OpenGLShaderProgram>(context);
    if (program->addVertexShader(vertexShader) && program->addFragmentShader(fragmentShader) && program->link())
        shader = std::move(program);
    else
        DBG("Surface preview shader failed: " << program->getLastError());
}

void SurfacePreview::renderOpenGL()
{
    juce::OpenGLHelpers::clear(juce::Colour(0xff111317));
    if (shader == nullptr)
        return;

    {
        std::lock_guard<std::mutex> lock(pendingLock);
        if (pendingChanged)
        {
            pendingChanged = false;
            haveMaps = pendingBase.isValid() && pendingNormal.isValid() && pendingOrm.isValid();
            if (haveMaps)
            {
                baseTexture.loadImage(pendingBase);
                normalTexture.loadImage(pendingNormal);
                ormTexture.loadImage(pendingOrm);
                for (auto* texture : { &baseTexture, &normalTexture, &ormTexture })
                {
                    texture->bind();
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                }
            }
        }
    }

    auto& ext = context.extensions;
    const float scale = static_cast<float>(context.getRenderingScale());
    glViewport(0, 0, juce::roundToInt(scale * static_cast<float>(getWidth())), juce::roundToInt(scale * static_cast<float>(getHeight())));
    glEnable(GL_DEPTH_TEST);

    const float aspect = static_cast<float>(getWidth()) / static_cast<float>(juce::jmax(1, getHeight()));
    const float f = 1.0f / std::tan(juce::MathConstants<float>::pi / 8.0f);
    juce::Matrix3D<float> projection;
    projection.mat[0] = f / aspect;
    projection.mat[5] = f;
    projection.mat[10] = -100.1f / 99.9f;
    projection.mat[11] = -1.0f;
    projection.mat[14] = -20.0f / 99.9f;
    projection.mat[15] = 0.0f;

    juce::Matrix3D<float> view;
    view.mat[14] = -distance.load();
    const auto model = rotationYMatrix(rotationY.load()) * rotationXMatrix(rotationX.load());

    shader->use();
    const auto id = shader->getProgramID();
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(id, "projection"), 1, GL_FALSE, projection.mat);
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(id, "view"), 1, GL_FALSE, view.mat);
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(id, "model"), 1, GL_FALSE, model.mat);
    const float yaw = lightYaw.load(), pitch = lightPitch.load();
    ext.glUniform3f(ext.glGetUniformLocation(id, "lightDir"), std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    ext.glUniform3f(ext.glGetUniformLocation(id, "cameraPos"), 0.0f, 0.0f, distance.load());
    ext.glUniform1i(ext.glGetUniformLocation(id, "hasMaps"), haveMaps ? 1 : 0);
    ext.glUniform1i(ext.glGetUniformLocation(id, "directX"), directX.load() ? 1 : 0);

    if (haveMaps)
    {
        const char* names[3] = { "baseMap", "normalMap", "ormMap" };
        juce::OpenGLTexture* textures[3] = { &baseTexture, &normalTexture, &ormTexture };
        for (int unit = 0; unit < 3; ++unit)
        {
            ext.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
            textures[unit]->bind();
            ext.glUniform1i(ext.glGetUniformLocation(id, names[unit]), unit);
        }
    }

    const Mesh& mesh = shape == 2 ? cube : (shape == 3 ? plane : sphere);
    ext.glBindVertexArray(vertexArray);
    ext.glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    ext.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    const int offsets[4] = { 0, 3, 6, 8 }, sizes[4] = { 3, 3, 2, 3 };
    for (GLuint a = 0; a < 4; ++a)
    {
        ext.glEnableVertexAttribArray(a);
        ext.glVertexAttribPointer(a, sizes[a], GL_FLOAT, GL_FALSE, stride * static_cast<GLsizei>(sizeof(float)),
                                  reinterpret_cast<void*>(static_cast<size_t>(offsets[a]) * sizeof(float)));
    }
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    for (GLuint a = 0; a < 4; ++a)
        ext.glDisableVertexAttribArray(a);
    ext.glActiveTexture(GL_TEXTURE0);
    glDisable(GL_DEPTH_TEST);
}

void SurfacePreview::openGLContextClosing()
{
    shader.reset();
    baseTexture.release();
    normalTexture.release();
    ormTexture.release();
    auto& ext = context.extensions;
    for (auto* mesh : { &sphere, &cube, &plane })
    {
        if (mesh->vbo != 0) ext.glDeleteBuffers(1, &mesh->vbo);
        if (mesh->ebo != 0) ext.glDeleteBuffers(1, &mesh->ebo);
        *mesh = {};
    }
    if (vertexArray != 0)
        ext.glDeleteVertexArrays(1, &vertexArray);
    vertexArray = 0;
}
