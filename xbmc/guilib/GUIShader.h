/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

/*!
\file guiShader.h
\brief
*/

#include "GUIControl.h"
#include "guilib/guiinfo/GUIInfoLabel.h"
#include "Shader.h"

#include <vector>

/*!
 \ingroup controls
 \brief
 */

class CGUIShader : public CGUIControl
{
public:
  CGUIShader(int parentID, int controlID, float posX, float posY, float width, float height, const std::string& vertexShader, const std::string& fragmentShader);
  CGUIShader(const CGUIShader &left);
  ~CGUIShader(void) override;
  CGUIShader* Clone() const override { return new CGUIShader(*this); }

  void Process(unsigned int currentTime, CDirtyRegionList &dirtyregions) override;
  void Render() override;
  void UpdateVisibility(const CGUIListItem *item = NULL) override;
  bool OnAction(const CAction &action) override ;
  bool OnMessage(CGUIMessage& message) override;
  void SetFocus(bool focused) override;
  void AllocResources() override;
  void FreeResources(bool immediately = false) override;
  void DynamicResourceAlloc(bool bOnOff) override;
  bool IsDynamicallyAllocated() override { return m_bDynamicResourceAlloc; }
  void SetInvalid() override;
  bool CanFocus() const override;
  void UpdateInfo(const CGUIListItem *item = NULL) override;

  virtual void SetInfo(const KODI::GUILIB::GUIINFO::CGUIInfoLabel &info);
  virtual void SetVertexShader(const std::string& vertexShader, bool setConstant = false);
  virtual void SetFragmentShader(const std::string& fragmentShader, bool setConstant = false);
  virtual void SetShaderFile(const std::string& shaderFile, bool setConstant = false);
  virtual void SetAction(const std::string& action);
  void SetWidth(float width) override;
  void SetHeight(float height) override;
  void SetPosition(float posX, float posY) override;
  std::string GetDescription() const override;

  const std::string& GetVertexShader() const;
  const std::string& GetFragmentShader() const;

  CRect CalcRenderRegion() const override;

#ifdef _DEBUG
  void DumpTextureUse() override;
#endif
protected:
  virtual void AllocateOnDemand();
  virtual void FreeShaders(bool immediately = false);

  /*!
   * \brief Update the shader uniforms based on the current item infos
   * \param item the item to for info resolution
  */
  void UpdateUniforms(const CGUIListItem* item);

  bool m_bDynamicResourceAlloc;

  // shader info
  std::string m_vertexShader;
  std::string m_fragmentShader;
  std::string m_shaderFile; // Single shader file (frag or gles)
  KODI::GUILIB::GUIINFO::CGUIInfoLabel m_info;

  std::unique_ptr<Shaders::CShaderProgram> m_shaderProgram;
  std::string m_currentVertexShader;
  std::string m_currentFragmentShader;
  std::string m_currentShaderFile;

  unsigned int m_lastRenderTime;
  bool m_hasFocus; // Whether the control has focus
  std::string m_action; // Action mode: always or onfocus
  bool m_firstFrameRendered; // Whether the first frame has been rendered
  unsigned char* m_firstFrameBuffer; // Buffer to store the first frame
  static const int MAX_WIDTH = 1920; // Maximum width (1080p)
  static const int MAX_HEIGHT = 1080; // Maximum height (1080p)
};
