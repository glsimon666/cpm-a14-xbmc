/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIShader.h"

#include "ServiceBroker.h"
#include "addons/Skin.h"
#include "guilib/GUIComponent.h"
#include "guilib/Shader.h"
#include "rendering/gl/RenderSystemGL.h"
#include "utils/GLUtils.h"
#include "windowing/GraphicContext.h"

CGUIShader::CGUIShader(int parentID, int controlID, float posX, float posY, float width, float height, const std::string& vertexShader, const std::string& fragmentShader)
  : CGUIControl(parentID, controlID, posX, posY, width, height),
    m_bDynamicResourceAlloc(false),
    m_vertexShader(vertexShader),
    m_fragmentShader(fragmentShader),
    m_shaderFile(""),
    m_lastRenderTime(0),
    m_hasFocus(false),
    m_action("always"),
    m_firstFrameRendered(false),
    m_firstFrameBuffer(nullptr),
    m_renderSystem(nullptr)
{
  m_renderSystem = static_cast<CRenderSystemGL*>(CServiceBroker::GetRenderSystem());
}

CGUIShader::CGUIShader(const CGUIShader &left)
  : CGUIControl(left),
    m_bDynamicResourceAlloc(left.m_bDynamicResourceAlloc),
    m_vertexShader(left.m_vertexShader),
    m_fragmentShader(left.m_fragmentShader),
    m_shaderFile(left.m_shaderFile),
    m_info(left.m_info),
    m_currentVertexShader(left.m_currentVertexShader),
    m_currentFragmentShader(left.m_currentFragmentShader),
    m_currentShaderFile(left.m_currentShaderFile),
    m_lastRenderTime(left.m_lastRenderTime),
    m_hasFocus(left.m_hasFocus),
    m_action(left.m_action),
    m_firstFrameRendered(left.m_firstFrameRendered),
    m_firstFrameBuffer(nullptr)
{
  // Allocate new buffer for first frame if needed
  if (left.m_firstFrameBuffer && left.m_firstFrameRendered)
  {
    int width = (int)GetWidth();
    int height = (int)GetHeight();
    m_firstFrameBuffer = new unsigned char[width * height * 4];
    memcpy(m_firstFrameBuffer, left.m_firstFrameBuffer, width * height * 4);
  }
}

CGUIShader::~CGUIShader(void)
{
  FreeResources(true);
  if (m_firstFrameBuffer)
  {
    delete[] m_firstFrameBuffer;
    m_firstFrameBuffer = nullptr;
  }
}

void CGUIShader::Process(unsigned int currentTime, CDirtyRegionList &dirtyregions)
{
  if (m_bDynamicResourceAlloc && !m_shaderProgram && (m_currentVertexShader != "" || m_currentFragmentShader != ""))
    AllocateOnDemand();

  CGUIControl::Process(currentTime, dirtyregions);

  m_lastRenderTime = currentTime;
}

void CGUIShader::Render()
{
  // Check if we should render the shader
  bool shouldRender = false;
  
  // Check if parent has focus (for button integration)
  bool parentHasFocus = false;
  if (m_parentControl)
  {
    parentHasFocus = m_parentControl->HasFocus();
  }
  
  // Determine if this control has focus (either directly or through parent)
  bool controlHasFocus = m_hasFocus || parentHasFocus;
  
  if (m_action == "always")
  {
    shouldRender = true;
  }
  else if (m_action == "onfocus")
  {
    shouldRender = controlHasFocus;
  }

  if (shouldRender && m_shaderProgram && m_shaderProgram->OK())
  {
    // Enable the shader
    if (m_shaderProgram->Enable())
    {
      // Set up the shader uniforms
      UpdateUniforms(NULL);

      // Draw a quad with resolution limit
      CRect rect = GetRenderRegion();
      
      // Limit resolution to 1080p
      float width = rect.Width();
      float height = rect.Height();
      if (width > MAX_WIDTH || height > MAX_HEIGHT)
      {
        // Calculate aspect ratio
        float aspectRatio = width / height;
        if (aspectRatio > 1.0f) // Landscape
        {
          width = MAX_WIDTH;
          height = width / aspectRatio;
          if (height > MAX_HEIGHT)
          {
            height = MAX_HEIGHT;
            width = height * aspectRatio;
          }
        }
        else // Portrait
        {
          height = MAX_HEIGHT;
          width = height * aspectRatio;
          if (width > MAX_WIDTH)
          {
            width = MAX_WIDTH;
            height = width / aspectRatio;
          }
        }
        // Calculate new position to center the limited quad
        float posX = rect.x1 + (rect.Width() - width) / 2;
        float posY = rect.y1 + (rect.Height() - height) / 2;
        rect = CRect(posX, posY, posX + width, posY + height);
      }
      
      // Draw a quad using modern OpenGL
      struct PackedVertex {
        float x, y, z;
        float u1, v1;
      };
      
      PackedVertex vertices[4];
      
      // TopLeft
      vertices[0].x = rect.x1;
      vertices[0].y = rect.y1;
      vertices[0].z = 0.0f;
      vertices[0].u1 = 0.0f;
      vertices[0].v1 = 0.0f;
      
      // TopRight
      vertices[1].x = rect.x2;
      vertices[1].y = rect.y1;
      vertices[1].z = 0.0f;
      vertices[1].u1 = 1.0f;
      vertices[1].v1 = 0.0f;
      
      // BottomRight
      vertices[2].x = rect.x2;
      vertices[2].y = rect.y2;
      vertices[2].z = 0.0f;
      vertices[2].u1 = 1.0f;
      vertices[2].v1 = 1.0f;
      
      // BottomLeft
      vertices[3].x = rect.x1;
      vertices[3].y = rect.y2;
      vertices[3].z = 0.0f;
      vertices[3].u1 = 0.0f;
      vertices[3].v1 = 1.0f;
      
      GLushort indices[] = {0, 1, 2, 0, 2, 3};
      
      GLuint VertexVBO;
      GLuint IndexVBO;
      
      glGenBuffers(1, &VertexVBO);
      glBindBuffer(GL_ARRAY_BUFFER, VertexVBO);
      glBufferData(GL_ARRAY_BUFFER, sizeof(PackedVertex)*4, &vertices[0], GL_STATIC_DRAW);
      
      GLint posLoc = m_renderSystem->ShaderGetPos();
      GLint tex0Loc = m_renderSystem->ShaderGetCoord0();
      
      glVertexAttribPointer(posLoc, 3, GL_FLOAT, 0, sizeof(PackedVertex),
                            reinterpret_cast<const GLvoid*>(offsetof(PackedVertex, x)));
      glEnableVertexAttribArray(posLoc);
      glVertexAttribPointer(tex0Loc, 2, GL_FLOAT, 0, sizeof(PackedVertex),
                            reinterpret_cast<const GLvoid*>(offsetof(PackedVertex, u1)));
      glEnableVertexAttribArray(tex0Loc);
      
      glGenBuffers(1, &IndexVBO);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IndexVBO);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
      
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
      
      glDisableVertexAttribArray(posLoc);
      glDisableVertexAttribArray(tex0Loc);
      
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
      glDeleteBuffers(1, &VertexVBO);
      glDeleteBuffers(1, &IndexVBO);

      // Capture the first frame if not already done
      if (!m_firstFrameRendered)
      {
        int frameWidth = (int)width;
        int frameHeight = (int)height;
        if (frameWidth > 0 && frameHeight > 0)
        {
          if (m_firstFrameBuffer)
            delete[] m_firstFrameBuffer;
          m_firstFrameBuffer = new unsigned char[frameWidth * frameHeight * 4];
          // TODO: Capture the frame buffer here
          // This would require access to the OpenGL framebuffer
          m_firstFrameRendered = true;
        }
      }

      // Disable the shader
      m_shaderProgram->Disable();
    }
  }
  else if (!shouldRender && m_firstFrameRendered && m_firstFrameBuffer)
  {
    // Draw the first frame
    CRect rect = GetRenderRegion();
    // TODO: Draw the captured first frame here
    // This would require creating a texture from the buffer and rendering it
  }

  CGUIControl::Render();
}

void CGUIShader::UpdateVisibility(const CGUIListItem *item)
{
  CGUIControl::UpdateVisibility(item);
}

bool CGUIShader::OnAction(const CAction &action)
{
  return CGUIControl::OnAction(action);
}

bool CGUIShader::OnMessage(CGUIMessage& message)
{
  return CGUIControl::OnMessage(message);
}

void CGUIShader::AllocResources()
{
  if (m_bDynamicResourceAlloc)
    return;

  FreeResources(false);

  // Load and compile the shader
  if (!m_shaderFile.empty())
  {
    // Get the full path to the shader file from the skin's media folder
    std::string shaderPath = g_SkinInfo->GetSkinPath("media/" + m_shaderFile);

    m_shaderProgram = std::make_unique<Shaders::CGLSLShaderProgram>();
    // Determine shader type based on file extension
    if (shaderPath.substr(shaderPath.find_last_of(".") + 1) == "frag")
    {
      // Load as fragment shader with default vertex shader
      std::string defaultVertexShader = g_SkinInfo->GetSkinPath("media/default.vert");
      if (m_shaderProgram->VertexShader()->LoadSource(defaultVertexShader) &&
          m_shaderProgram->PixelShader()->LoadSource(shaderPath) &&
          m_shaderProgram->CompileAndLink())
      {
        m_currentShaderFile = m_shaderFile;
      }
      else
      {
        m_shaderProgram.reset();
      }
    }
    else
    {
      // For other types, use as both vertex and fragment shader
      // This is a placeholder, actual implementation would depend on shader type
      m_shaderProgram.reset();
    }
  }
  else if (!m_vertexShader.empty() && !m_fragmentShader.empty())
  {
    // Get the full path to the shader files from the skin's media folder
    std::string vertexShaderPath = g_SkinInfo->GetSkinPath("media/" + m_vertexShader);
    std::string fragmentShaderPath = g_SkinInfo->GetSkinPath("media/" + m_fragmentShader);

    m_shaderProgram = std::make_unique<Shaders::CGLSLShaderProgram>();
    if (m_shaderProgram->VertexShader()->LoadSource(vertexShaderPath) &&
        m_shaderProgram->PixelShader()->LoadSource(fragmentShaderPath) &&
        m_shaderProgram->CompileAndLink())
    {
      m_currentVertexShader = m_vertexShader;
      m_currentFragmentShader = m_fragmentShader;
    }
    else
    {
      m_shaderProgram.reset();
    }
  }

  CGUIControl::AllocResources();
}

void CGUIShader::FreeResources(bool immediately)
{
  FreeShaders(immediately);
  CGUIControl::FreeResources(immediately);
}

void CGUIShader::DynamicResourceAlloc(bool bOnOff)
{
  m_bDynamicResourceAlloc = bOnOff;
  if (!bOnOff)
    AllocResources();
  else
    FreeResources(false);
}

void CGUIShader::SetInvalid()
{
  CGUIControl::SetInvalid();
}

bool CGUIShader::CanFocus() const
{
  return false;
}

void CGUIShader::UpdateInfo(const CGUIListItem *item)
{
  CGUIControl::UpdateInfo(item);
}

void CGUIShader::SetInfo(const KODI::GUILIB::GUIINFO::CGUIInfoLabel &info)
{
  m_info = info;
}

void CGUIShader::SetVertexShader(const std::string& vertexShader, bool setConstant)
{
  if (setConstant)
    m_vertexShader = vertexShader;
  else
    m_vertexShader = vertexShader;

  if (!m_bDynamicResourceAlloc)
    AllocResources();
  else
    m_shaderProgram.reset();
}

void CGUIShader::SetFragmentShader(const std::string& fragmentShader, bool setConstant)
{
  if (setConstant)
    m_fragmentShader = fragmentShader;
  else
    m_fragmentShader = fragmentShader;

  if (!m_bDynamicResourceAlloc)
    AllocResources();
  else
    m_shaderProgram.reset();
}

void CGUIShader::SetShaderFile(const std::string& shaderFile, bool setConstant)
{
  if (setConstant)
    m_shaderFile = shaderFile;
  else
    m_shaderFile = shaderFile;

  if (!m_bDynamicResourceAlloc)
    AllocResources();
  else
    m_shaderProgram.reset();
}

void CGUIShader::SetAction(const std::string& action)
{
  m_action = action;
  if (m_action != "always" && m_action != "onfocus")
    m_action = "always"; // Default to always
}

void CGUIShader::SetFocus(bool focused)
{
  m_hasFocus = focused;
  CGUIControl::SetFocus(focused);
}

void CGUIShader::SetWidth(float width)
{
  CGUIControl::SetWidth(width);
}

void CGUIShader::SetHeight(float height)
{
  CGUIControl::SetHeight(height);
}

void CGUIShader::SetPosition(float posX, float posY)
{
  CGUIControl::SetPosition(posX, posY);
}

std::string CGUIShader::GetDescription() const
{
  return "Shader Control";
}

const std::string& CGUIShader::GetVertexShader() const
{
  return m_vertexShader;
}

const std::string& CGUIShader::GetFragmentShader() const
{
  return m_fragmentShader;
}

CRect CGUIShader::CalcRenderRegion() const
{
  return GetRenderRegion();
}

#ifdef _DEBUG
void CGUIShader::DumpTextureUse()
{
  CGUIControl::DumpTextureUse();
}
#endif

void CGUIShader::AllocateOnDemand()
{
  if (m_shaderProgram)
    return;

  if (!m_shaderFile.empty())
  {
    // Get the full path to the shader file from the skin's media folder
    std::string shaderPath = g_SkinInfo->GetSkinPath("media/" + m_shaderFile);

    m_shaderProgram = std::make_unique<Shaders::CGLSLShaderProgram>();
    // Determine shader type based on file extension
    if (shaderPath.substr(shaderPath.find_last_of(".") + 1) == "frag")
    {
      // Load as fragment shader with default vertex shader
      std::string defaultVertexShader = g_SkinInfo->GetSkinPath("media/default.vert");
      if (m_shaderProgram->VertexShader()->LoadSource(defaultVertexShader) &&
          m_shaderProgram->PixelShader()->LoadSource(shaderPath) &&
          m_shaderProgram->CompileAndLink())
      {
        m_currentShaderFile = m_shaderFile;
      }
      else
      {
        m_shaderProgram.reset();
      }
    }
    else
    {
      // For other types, use as both vertex and fragment shader
      // This is a placeholder, actual implementation would depend on shader type
      m_shaderProgram.reset();
    }
  }
  else if (!m_vertexShader.empty() && !m_fragmentShader.empty())
  {
    // Get the full path to the shader files from the skin's media folder
    std::string vertexShaderPath = g_SkinInfo->GetSkinPath("media/" + m_vertexShader);
    std::string fragmentShaderPath = g_SkinInfo->GetSkinPath("media/" + m_fragmentShader);

    m_shaderProgram = std::make_unique<Shaders::CGLSLShaderProgram>();
    if (m_shaderProgram->VertexShader()->LoadSource(vertexShaderPath) &&
        m_shaderProgram->PixelShader()->LoadSource(fragmentShaderPath) &&
        m_shaderProgram->CompileAndLink())
    {
      m_currentVertexShader = m_vertexShader;
      m_currentFragmentShader = m_fragmentShader;
    }
    else
    {
      m_shaderProgram.reset();
    }
  }
}

void CGUIShader::FreeShaders(bool immediately)
{
  m_shaderProgram.reset();
  m_currentVertexShader.clear();
  m_currentFragmentShader.clear();
  m_currentShaderFile.clear();
  
  if (m_firstFrameBuffer)
  {
    delete[] m_firstFrameBuffer;
    m_firstFrameBuffer = nullptr;
  }
  m_firstFrameRendered = false;
}

void CGUIShader::UpdateUniforms(const CGUIListItem* item)
{
  if (!m_shaderProgram || !m_shaderProgram->OK())
    return;

  // Set basic uniforms
  // Get the current time in seconds
  float time = (float)m_lastRenderTime / 1000.0f;
  
  // Get the resolution
  CRect rect = GetRenderRegion();
  float width = rect.Width();
  float height = rect.Height();
  
  // Limit resolution to 1080p
  if (width > MAX_WIDTH || height > MAX_HEIGHT)
  {
    float aspectRatio = width / height;
    if (aspectRatio > 1.0f) // Landscape
    {
      width = MAX_WIDTH;
      height = width / aspectRatio;
      if (height > MAX_HEIGHT)
      {
        height = MAX_HEIGHT;
        width = height * aspectRatio;
      }
    }
    else // Portrait
    {
      height = MAX_HEIGHT;
      width = height * aspectRatio;
      if (width > MAX_WIDTH)
      {
        width = MAX_WIDTH;
        height = width / aspectRatio;
      }
    }
  }
  
  // Set uniforms for the shader
  // These are common uniforms used in many shaders
  GLint timeLocation = glGetUniformLocation(m_shaderProgram->ProgramHandle(), "iTime");
  if (timeLocation != -1)
    glUniform1f(timeLocation, time);
  
  GLint resolutionLocation = glGetUniformLocation(m_shaderProgram->ProgramHandle(), "iResolution");
  if (resolutionLocation != -1)
    glUniform2f(resolutionLocation, width, height);
  
  GLint timeLocation2 = glGetUniformLocation(m_shaderProgram->ProgramHandle(), "time");
  if (timeLocation2 != -1)
    glUniform1f(timeLocation2, time);
  
  GLint resolutionLocation2 = glGetUniformLocation(m_shaderProgram->ProgramHandle(), "resolution");
  if (resolutionLocation2 != -1)
    glUniform2f(resolutionLocation2, width, height);
}
