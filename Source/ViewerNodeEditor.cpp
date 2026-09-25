#include "ViewerNodeEditor.h"
#include "DjehutiHeadData.h"
#include <cmath>
#include <vector>

ViewerNodeEditor::ViewerNodeEditor()
{
    viewModeSelector.addItem("2D Plane", 1);
    viewModeSelector.addItem("3D Cube", 2);
    viewModeSelector.addItem("3D Sphere", 3);
    viewModeSelector.addItem("3D Cylinder", 4);
    viewModeSelector.addItem("3D Cone", 5);
    viewModeSelector.addItem("Djehuti (Ibis) Head", 6);
    viewModeSelector.setSelectedId(2);
    addAndMakeVisible(viewModeSelector);
    
    addAndMakeVisible(compileButton);
    addAndMakeVisible(saveButton);

    compileButton.onClick = [this]() {
        if (onCompileRequested) onCompileRequested();
    };
    saveButton.onClick = [this]() {
        if (onSaveRequested) onSaveRequested();
    };

    auto defaultSnapshot = std::make_shared<TextureFrameSnapshot>();
    defaultSnapshot->debugColour = juce::Colour(0xff121212);
    defaultSnapshot->generatedGlsl = "void EvaluateMaterial(in vec2 vUV, in vec3 worldPosition, in vec3 worldNormal, in vec3 cameraVector, in float time, out vec3 baseColor, out float metallic, out float roughness, out vec3 normal) { float f = mod(floor(vUV.x * 10.0) + floor(vUV.y * 10.0), 2.0); baseColor = vec3(f * 0.5 + 0.2); metallic = 0.0; roughness = 0.5; normal = vec3(0.5, 0.5, 1.0); }";
    publishedSnapshot.store(defaultSnapshot);

    openGLContext.setRenderer(this);
    openGLContext.setContinuousRepainting(true);
    openGLContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    openGLContext.attachTo(*this);

    setSize(400, 400);
}

ViewerNodeEditor::~ViewerNodeEditor()
{
    openGLContext.detach();
}

void ViewerNodeEditor::setSnapshot(std::shared_ptr<const TextureFrameSnapshot> snapshot)
{
    publishedSnapshot.store(snapshot);
}

static void CreateBuffer(juce::OpenGLContext& ctx, ViewerNodeEditor::Geometry& geo, const std::vector<float>& v, const std::vector<unsigned int>& i) {
    ctx.extensions.glGenBuffers(1, &geo.vbo);
    ctx.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, geo.vbo);
    ctx.extensions.glBufferData(juce::gl::GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), juce::gl::GL_STATIC_DRAW);
    ctx.extensions.glGenBuffers(1, &geo.ebo);
    ctx.extensions.glBindBuffer(juce::gl::GL_ELEMENT_ARRAY_BUFFER, geo.ebo);
    ctx.extensions.glBufferData(juce::gl::GL_ELEMENT_ARRAY_BUFFER, i.size() * sizeof(unsigned int), i.data(), juce::gl::GL_STATIC_DRAW);
    geo.numIndices = i.size();
}

void ViewerNodeEditor::buildPlane() {
    std::vector<float> v = {
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f
    };
    std::vector<unsigned int> i = { 0,1,2, 2,3,0 };
    CreateBuffer(openGLContext, plane, v, i);
}

void ViewerNodeEditor::buildCube() {
    std::vector<float> v = {
        -0.5f,-0.5f, 0.5f,  0,0,1,  0,1,   0.5f,-0.5f, 0.5f,  0,0,1,  1,1,   0.5f, 0.5f, 0.5f,  0,0,1,  1,0,  -0.5f, 0.5f, 0.5f,  0,0,1,  0,0,
         0.5f,-0.5f,-0.5f,  0,0,-1, 0,1,  -0.5f,-0.5f,-0.5f,  0,0,-1, 1,1,  -0.5f, 0.5f,-0.5f,  0,0,-1, 1,0,   0.5f, 0.5f,-0.5f,  0,0,-1, 0,0,
        -0.5f,-0.5f,-0.5f, -1,0,0,  0,1,  -0.5f,-0.5f, 0.5f, -1,0,0,  1,1,  -0.5f, 0.5f, 0.5f, -1,0,0,  1,0,  -0.5f, 0.5f,-0.5f, -1,0,0,  0,0,
         0.5f,-0.5f, 0.5f,  1,0,0,  0,1,   0.5f,-0.5f,-0.5f,  1,0,0,  1,1,   0.5f, 0.5f,-0.5f,  1,0,0,  1,0,   0.5f, 0.5f, 0.5f,  1,0,0,  0,0,
        -0.5f, 0.5f, 0.5f,  0,1,0,  0,1,   0.5f, 0.5f, 0.5f,  0,1,0,  1,1,   0.5f, 0.5f,-0.5f,  0,1,0,  1,0,  -0.5f, 0.5f,-0.5f,  0,1,0,  0,0,
        -0.5f,-0.5f,-0.5f,  0,-1,0, 0,1,   0.5f,-0.5f,-0.5f,  0,-1,0, 1,1,   0.5f,-0.5f, 0.5f,  0,-1,0, 1,0,  -0.5f,-0.5f, 0.5f,  0,-1,0, 0,0
    };
    std::vector<unsigned int> i = { 0,1,2,2,3,0, 4,5,6,6,7,4, 8,9,10,10,11,8, 12,13,14,14,15,12, 16,17,18,18,19,16, 20,21,22,22,23,20 };
    CreateBuffer(openGLContext, cube, v, i);
}

void ViewerNodeEditor::buildSphere(int segments) {
    std::vector<float> v; std::vector<unsigned int> i;
    for(int y=0; y<=segments; y++) {
        for(int x=0; x<=segments; x++) {
            float xSeg = (float)x/(float)segments, ySeg = (float)y/(float)segments;
            float px = std::cos(xSeg * 6.283185f) * std::sin(ySeg * 3.14159f);
            float py = std::cos(ySeg * 3.14159f);
            float pz = std::sin(xSeg * 6.283185f) * std::sin(ySeg * 3.14159f);
            v.push_back(px*0.75f); v.push_back(py*0.75f); v.push_back(pz*0.75f);
            v.push_back(px); v.push_back(py); v.push_back(pz);
            v.push_back(xSeg); v.push_back(ySeg);
        }
    }
    for(int y=0; y<segments; y++) {
        for(int x=0; x<segments; x++) {
            i.push_back((y+1)*(segments+1) + x); i.push_back(y*(segments+1) + x); i.push_back(y*(segments+1) + x + 1);
            i.push_back((y+1)*(segments+1) + x); i.push_back(y*(segments+1) + x + 1); i.push_back((y+1)*(segments+1) + x + 1);
        }
    }
    CreateBuffer(openGLContext, sphere, v, i);
}

void ViewerNodeEditor::buildCylinder(int segments) {
    std::vector<float> v; std::vector<unsigned int> i;
    for(int p=0; p<=segments; p++) {
        float a = ((float)p/segments) * 6.283185f;
        float px = std::cos(a)*0.5f, pz = std::sin(a)*0.5f;
        v.push_back(px); v.push_back(-0.5f); v.push_back(pz); v.push_back(px); v.push_back(0); v.push_back(pz); v.push_back((float)p/segments); v.push_back(1.0f);
        v.push_back(px); v.push_back( 0.5f); v.push_back(pz); v.push_back(px); v.push_back(0); v.push_back(pz); v.push_back((float)p/segments); v.push_back(0.0f);
    }
    for(int p=0; p<segments; p++) {
        int b=p*2; i.push_back(b); i.push_back(b+1); i.push_back(b+3); i.push_back(b); i.push_back(b+3); i.push_back(b+2);
    }
    CreateBuffer(openGLContext, cylinder, v, i);
}

void ViewerNodeEditor::buildCone(int segments) {
    std::vector<float> v; std::vector<unsigned int> i;
    for(int p=0; p<=segments; p++) {
        float a = ((float)p/segments) * 6.283185f;
        float px = std::cos(a)*0.5f, pz = std::sin(a)*0.5f;
        float l = std::sqrt(px*px+0.25f+pz*pz); 
        float nx = px/l, ny = 0.5f/l, nz = pz/l;
        v.push_back(px); v.push_back(-0.5f); v.push_back(pz); v.push_back(nx); v.push_back(ny); v.push_back(nz); v.push_back((float)p/segments); v.push_back(1.0f);
        v.push_back(0.0f); v.push_back( 0.5f); v.push_back(0.0f); v.push_back(nx); v.push_back(ny); v.push_back(nz); v.push_back((float)p/segments); v.push_back(0.0f);
    }
    for(int p=0; p<segments; p++) {
        int b=p*2; i.push_back(b); i.push_back(b+1); i.push_back(b+3); i.push_back(b); i.push_back(b+3); i.push_back(b+2);
    }
    CreateBuffer(openGLContext, cone, v, i);
}

void ViewerNodeEditor::buildDjehuti() {
    CreateBuffer(openGLContext, djehuti, creation_texture::djehutiVertices, creation_texture::djehutiIndices);
}

void ViewerNodeEditor::createGeometries() {
    buildPlane(); buildCube(); buildSphere(32); buildCylinder(32); buildCone(32); buildDjehuti();
}

void ViewerNodeEditor::compileShaderProgram(const std::string& evaluateTextureFunc)
{
    const char* vShader = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aNormal;
        layout(location = 2) in vec2 aUV;
        uniform mat4 projection;
        uniform mat4 view;
        uniform mat4 model;
        out vec2 vUV;
        out vec3 vNormal;
        out vec3 vWorldPos;
        void main() {
            vUV = aUV;
            vNormal = mat3(model) * aNormal;
            vec4 wPos = model * vec4(aPos, 1.0);
            vWorldPos = wPos.xyz;
            gl_Position = projection * view * wPos;
        }
    )";
    
    std::string fShader = R"(
        #version 330 core
        in vec2 vUV;
        in vec3 vNormal;
        in vec3 vWorldPos;
        out vec4 FragColor;
        
        )" + evaluateTextureFunc + R"(
        
        void main() {
            vec3 baseColor;
            float metallic;
            float roughness;
            vec3 normal;
            vec3 cameraVector = normalize(-vWorldPos);
            EvaluateMaterial(vUV, vWorldPos, vNormal, cameraVector, 0.0, baseColor, metallic, roughness, normal);
            
            vec3 N = normalize(vNormal);
            // In a real PBR shader, 'normal' from EvaluateMaterial (which is a tangent-space normal map)
            // would be transformed using TBN. For this basic preview, if 'normal' is default (0.5,0.5,1.0),
            // we just use N.
            vec3 mappedNormal = normal * 2.0 - 1.0;
            // Hacky non-tangent bump:
            if (length(mappedNormal) > 0.1) {
                // N = normalize(N + mappedNormal * 0.2);
            }
            
            vec3 V = normalize(-vWorldPos); // simple camera at 0,0,0 in view space, so just -worldPos roughly
            vec3 L = normalize(vec3(0.5, 1.0, 0.5));
            vec3 H = normalize(V + L);
            
            float NdotL = max(dot(N, L), 0.0);
            float NdotH = max(dot(N, H), 0.0);
            
            vec3 F0 = mix(vec3(0.04), baseColor, metallic);
            
            // Basic Cook-Torrance style specular
            float alpha = max(roughness * roughness, 0.001);
            float D = alpha*alpha / (3.14159 * pow(NdotH*NdotH * (alpha*alpha - 1.0) + 1.0, 2.0));
            
            vec3 specular = F0 * D;
            vec3 diffuse = baseColor * (1.0 - metallic) * NdotL;
            
            vec3 ambient = baseColor * 0.1;
            
            vec3 finalColor = ambient + diffuse + specular;
            
            // Gamma correction
            finalColor = pow(finalColor, vec3(1.0/2.2));
            
            FragColor = vec4(finalColor, 1.0);
        }
    )";
    
    auto newProgram = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    if (newProgram->addVertexShader(vShader) && newProgram->addFragmentShader(fShader) && newProgram->link()) {
        shaderProgram = std::move(newProgram);
        currentShaderCode = evaluateTextureFunc;
        failedShaderCode.clear();
    } else {
        failedShaderCode = evaluateTextureFunc;
        DBG("Material preview shader failed to compile: " << newProgram->getLastError());
    }
}

void ViewerNodeEditor::newOpenGLContextCreated()
{
    openGLContext.extensions.glGenVertexArrays(1, &vertexArray);
    openGLContext.extensions.glBindVertexArray(vertexArray);
    createGeometries();
    compileShaderProgram("void EvaluateMaterial(in vec2 vUV, in vec3 worldPosition, in vec3 worldNormal, in vec3 cameraVector, in float time, out vec3 baseColor, out float metallic, out float roughness, out vec3 normal) { baseColor = vec3(vUV.x, vUV.y, 1.0); metallic=0.0; roughness=0.5; normal=vec3(0.5,0.5,1.0); }\nvec3 EvaluateWorldPositionOffset(in vec3 localPosition, in vec3 worldPosition, in vec3 worldNormal, in vec3 cameraVector, in float time, in vec2 vUV) { return vec3(0.0); }");
}

void ViewerNodeEditor::renderOpenGL()
{
    auto currentSnapshot = publishedSnapshot.load();
    if (!currentSnapshot) return;

    if (currentSnapshot->generatedGlsl != currentShaderCode && currentSnapshot->generatedGlsl != failedShaderCode
        && !currentSnapshot->generatedGlsl.empty()) {
        compileShaderProgram(currentSnapshot->generatedGlsl);
        loadedTextures.clear();
    }

    juce::OpenGLHelpers::clear(currentSnapshot->debugColour);
    
    if (!shaderProgram) return;

    
    
    float aspect = getWidth() / (float)juce::jmax(1, getHeight());
    juce::Matrix3D<float> proj;
    proj.mat[0] = 1.0f / (aspect * std::tan(juce::MathConstants<float>::pi / 4.0f));
    proj.mat[5] = 1.0f / std::tan(juce::MathConstants<float>::pi / 4.0f);
    proj.mat[10] = -100.1f / 99.9f;
    proj.mat[11] = -1.0f;
    proj.mat[14] = -20.0f / 99.9f;
    proj.mat[15] = 0.0f;
    
    juce::Matrix3D<float> view;
    view.mat[14] = -cameraDistance; 
    

    juce::Matrix3D<float> model;
    int mode = viewModeSelector.getSelectedId();
    if (mode == 1) {
        model = juce::Matrix3D<float>::rotation({0.0f, 0.0f, 0.0f});
        model.mat[13] = -0.7f;
        model.mat[0] = 1.5f; model.mat[5] = 1.5f; 
    } else {
        juce::Matrix3D<float> modelRotX;
        modelRotX.mat[5] = std::cos(rotationX);
        modelRotX.mat[6] = std::sin(rotationX);
        modelRotX.mat[9] = -std::sin(rotationX);
        modelRotX.mat[10] = std::cos(rotationX);

        juce::Matrix3D<float> modelRotY;
        modelRotY.mat[0] = std::cos(rotationY);
        modelRotY.mat[2] = -std::sin(rotationY);
        modelRotY.mat[8] = std::sin(rotationY);
        modelRotY.mat[10] = std::cos(rotationY);

        model = modelRotY * modelRotX;
    }


    shaderProgram->use();
    openGLContext.extensions.glBindVertexArray(vertexArray);
    
    openGLContext.extensions.glUniformMatrix4fv(openGLContext.extensions.glGetUniformLocation(shaderProgram->getProgramID(), "projection"), 1, juce::gl::GL_FALSE, proj.mat);
    openGLContext.extensions.glUniformMatrix4fv(openGLContext.extensions.glGetUniformLocation(shaderProgram->getProgramID(), "view"), 1, juce::gl::GL_FALSE, view.mat);
    openGLContext.extensions.glUniformMatrix4fv(openGLContext.extensions.glGetUniformLocation(shaderProgram->getProgramID(), "model"), 1, juce::gl::GL_FALSE, model.mat);

    int unit = 0;
    for (const auto& slot : currentSnapshot->imageSlots) {
        if (loadedTextures.find(slot.uniformName) == loadedTextures.end()) {
            auto glTex = std::make_unique<juce::OpenGLTexture>();
            glTex->loadImage(slot.image);
            loadedTextures[slot.uniformName] = std::move(glTex);
        }
        
        auto& tex = loadedTextures[slot.uniformName];
        openGLContext.extensions.glActiveTexture(juce::gl::GL_TEXTURE0 + unit);
        tex->bind();
        openGLContext.extensions.glUniform1i(openGLContext.extensions.glGetUniformLocation(shaderProgram->getProgramID(), slot.uniformName.c_str()), unit);
        unit++;
    }

    Geometry* g = &plane;
    if (mode == 2) g = &cube;
    else if (mode == 3) g = &sphere;
    else if (mode == 4) g = &cylinder;
    else if (mode == 5) g = &cone;
    else if (mode == 6) g = &djehuti;

    openGLContext.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, g->vbo);
    openGLContext.extensions.glBindBuffer(juce::gl::GL_ELEMENT_ARRAY_BUFFER, g->ebo);
    
    openGLContext.extensions.glEnableVertexAttribArray(0);
    openGLContext.extensions.glEnableVertexAttribArray(1);
    openGLContext.extensions.glEnableVertexAttribArray(2);
    openGLContext.extensions.glVertexAttribPointer(0, 3, juce::gl::GL_FLOAT, juce::gl::GL_FALSE, 8 * sizeof(float), (void*)0);
    openGLContext.extensions.glVertexAttribPointer(1, 3, juce::gl::GL_FLOAT, juce::gl::GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    openGLContext.extensions.glVertexAttribPointer(2, 2, juce::gl::GL_FLOAT, juce::gl::GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    juce::gl::glEnable(juce::gl::GL_DEPTH_TEST);
    juce::gl::glDrawElements(juce::gl::GL_TRIANGLES, g->numIndices, juce::gl::GL_UNSIGNED_INT, 0);
    juce::gl::glDisable(juce::gl::GL_DEPTH_TEST);
    
    openGLContext.extensions.glDisableVertexAttribArray(0);
    openGLContext.extensions.glDisableVertexAttribArray(1);
    openGLContext.extensions.glDisableVertexAttribArray(2);
}

void ViewerNodeEditor::openGLContextClosing()
{
    shaderProgram.reset();
    auto del = [&](Geometry& g) {
        if(g.vbo) openGLContext.extensions.glDeleteBuffers(1, &g.vbo);
        if(g.ebo) openGLContext.extensions.glDeleteBuffers(1, &g.ebo);
    };
    del(plane); del(cube); del(sphere); del(cylinder); del(cone); del(djehuti);
    if (vertexArray != 0)
        openGLContext.extensions.glDeleteVertexArrays(1, &vertexArray);
    vertexArray = 0;
}

void ViewerNodeEditor::paint(juce::Graphics& g) {}

void ViewerNodeEditor::resized()
{
    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(30);
    viewModeSelector.setBounds(topBar.reduced(2).removeFromLeft(150));
    saveButton.setBounds(topBar.reduced(2).removeFromLeft(100));
    compileButton.setBounds(topBar.reduced(2).removeFromLeft(100));
}

void ViewerNodeEditor::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        isRightMouseDragging = true;
        lastMousePos = e.position;
    }
}

void ViewerNodeEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (isRightMouseDragging)
    {
        auto delta = e.position - lastMousePos;
        rotationY += delta.x * 0.01f;
        rotationX += delta.y * 0.01f;
        lastMousePos = e.position;
    }
}

void ViewerNodeEditor::mouseUp(const juce::MouseEvent& e)
{
    isRightMouseDragging = false;
}

void ViewerNodeEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    cameraDistance -= wheel.deltaY * 5.0f;
    if (cameraDistance < 0.5f) cameraDistance = 0.5f;
    if (cameraDistance > 20.0f) cameraDistance = 20.0f;
}
