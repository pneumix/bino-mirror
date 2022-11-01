/*
 * This file is part of Bino, a 3D video player.
 *
 * Copyright (C) 2022
 * Martin Lambers <marlam@marlam.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QGuiApplication>
#include <QMessageBox>
#include <QQuaternion>
#include <QtMath>

#include "widget.hpp"
#include "playlist.hpp"
#include "tools.hpp"
#include "log.hpp"

/* These might not be defined in OpenGL ES environments.
 * Define them here to fix compilation. */
#ifndef GL_BACK_LEFT
# define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_BACK_RIGHT
# define GL_BACK_RIGHT 0x0403
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
# define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif


static const QSize SizeBase(16, 9);

Widget::Widget(Bino* bino, StereoMode stereoMode, QWidget* parent) :
    QOpenGLWidget(parent),
    _bino(bino),
    _sizeHint(0.5f * SizeBase),
    _stereoMode(stereoMode),
    _openGLStereo(QSurfaceFormat::defaultFormat().stereo()),
    _alternatingLastView(1),
    _inThreeSixtyMovement(false),
    _threeSixtyHorizontalAngleBase(0.0f),
    _threeSixtyVerticalAngleBase(0.0f),
    _threeSixtyHorizontalAngleCurrent(0.0f),
    _threeSixtyVerticalAngleCurrent(0.0f)
{
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
    setMouseTracking(true);
    setMinimumSize(8, 8);
    QSize screenSize = QGuiApplication::primaryScreen()->availableSize();
    QSize maxSize = 0.75f * screenSize;
    _sizeHint = SizeBase.scaled(maxSize, Qt::KeepAspectRatio);
    connect(_bino, &Bino::newVideoFrame, [=]() { update(); });
    connect(_bino, &Bino::toggleFullscreen, [=]() { emit toggleFullscreen(); });
    connect(Playlist::instance(), SIGNAL(mediaChanged(PlaylistEntry)), this, SLOT(mediaChanged(PlaylistEntry)));
    setFocus();
}

bool Widget::isOpenGLStereo() const
{
    return _openGLStereo;
}

Widget::StereoMode Widget::stereoMode() const
{
    return _stereoMode;
}

void Widget::setStereoMode(enum StereoMode mode)
{
    _stereoMode = mode;
}

QSize Widget::sizeHint() const
{
    return _sizeHint;
}

void Widget::initializeGL()
{
    bool contextIsOk = (context()->isValid()
            && context()->format().majorVersion() >= 3
            && context()->format().minorVersion() >= 2);
    if (!contextIsOk) {
        LOG_FATAL("insufficient OpenGL capabilities");
        QMessageBox::critical(this, "Error", "Insufficient OpenGL capabilities.");
        std::exit(1);
    }
    if (QSurfaceFormat::defaultFormat().stereo() && !context()->format().stereo()) {
        LOG_FATAL("OpenGL stereo mode is not available on this system");
        QMessageBox::critical(this, "Error", "OpenGL stereo mode is not available on this system.");
        std::exit(1);
    }

    bool isGLES = QOpenGLContext::currentContext()->isOpenGLES();
    initializeOpenGLFunctions();

    // View textures
    glGenTextures(2, _viewTex);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, _viewTex[i]);
        unsigned char nullBytes[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        if (isGLES)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, 1, 1, 0, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, nullBytes);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, 1, 1, 0, GL_RGBA, GL_UNSIGNED_SHORT, nullBytes);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0f);
        _viewTexWidth[i] = 1;
        _viewTexHeight[i] = 1;
    }
    CHECK_GL();

    // Quad geometry
    const float quadPositions[] = {
        -1.0f, +1.0f, 0.0f,
        +1.0f, +1.0f, 0.0f,
        +1.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f
    };
    const float quadTexCoords[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f, 0.0f
    };
    static const unsigned short quadIndices[] = {
        0, 3, 1, 1, 3, 2
    };
    glGenVertexArrays(1, &_quadVao);
    glBindVertexArray(_quadVao);
    GLuint quadPositionBuf;
    glGenBuffers(1, &quadPositionBuf);
    glBindBuffer(GL_ARRAY_BUFFER, quadPositionBuf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadPositions), quadPositions, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    GLuint quadTexCoordBuf;
    glGenBuffers(1, &quadTexCoordBuf);
    glBindBuffer(GL_ARRAY_BUFFER, quadTexCoordBuf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadTexCoords), quadTexCoords, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);
    GLuint quadIndexBuf;
    glGenBuffers(1, &quadIndexBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadIndexBuf);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);
    CHECK_GL();

    // Shader program
    QString vertexShaderSource = readFile(":shader-display.vert.glsl");
    QString fragmentShaderSource = readFile(":shader-display.frag.glsl");
    if (isGLES) {
        vertexShaderSource.prepend("#version 320 es\n");
        fragmentShaderSource.prepend("#version 320 es\n"
                "precision mediump float;\n");
    } else {
        vertexShaderSource.prepend("#version 330\n");
        fragmentShaderSource.prepend("#version 330\n");
    }
    _prg.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    _prg.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    _prg.link();

    // Initialize Bino
    _bino->initProcess();
}

void Widget::paintGL()
{
    bool isGLES = QOpenGLContext::currentContext()->isOpenGLES();

    // Find out about the views we have
    int viewCount, viewWidth, viewHeight;
    float frameDisplayAspectRatio;
    bool threeSixty;
    _bino->preRenderProcess(_width, _height, &viewCount, &viewWidth, &viewHeight, &frameDisplayAspectRatio, &threeSixty);
    LOG_FIREHOSE("%s: %d views, %dx%d, %g, 360°=%s", Q_FUNC_INFO, viewCount, viewWidth, viewHeight, frameDisplayAspectRatio, threeSixty ? "on" : "off");

    // Adjust the stereo mode if necessary
    bool frameIsStereo = (viewCount == 2);
    StereoMode stereoMode = _stereoMode;
    if (!frameIsStereo)
        stereoMode = Mode_Left;

    // Fill the view texture(s) as needed
    for (int v = 0; v <= 1; v++) {
        bool needThisView = true;
        switch (stereoMode) {
        case Mode_Left:
            needThisView = (v == 0);
            break;
        case Mode_Right:
            needThisView = (v == 1);
            break;
        case Mode_Alternating:
            needThisView = (v != _alternatingLastView);
            break;
        case Mode_OpenGL_Stereo:
        case Mode_Red_Cyan_Dubois:
        case Mode_Red_Cyan_FullColor:
        case Mode_Red_Cyan_HalfColor:
        case Mode_Red_Cyan_Monochrome:
        case Mode_Green_Magenta_Dubois:
        case Mode_Green_Magenta_FullColor:
        case Mode_Green_Magenta_HalfColor:
        case Mode_Green_Magenta_Monochrome:
        case Mode_Amber_Blue_Dubois:
        case Mode_Amber_Blue_FullColor:
        case Mode_Amber_Blue_HalfColor:
        case Mode_Amber_Blue_Monochrome:
        case Mode_Red_Green_Monochrome:
        case Mode_Red_Blue_Monochrome:
            break;
        }
        if (!needThisView)
            continue;
        // prepare view texture
        glBindTexture(GL_TEXTURE_2D, _viewTex[v]);
        if (_viewTexWidth[v] != viewWidth || _viewTexHeight[v] != viewHeight) {
            if (isGLES)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, viewWidth, viewHeight, 0, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, nullptr);
            else
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, viewWidth, viewHeight, 0, GL_RGBA, GL_UNSIGNED_SHORT, nullptr);
            _viewTexWidth[v] = viewWidth;
            _viewTexHeight[v] = viewHeight;
        }
        // render view into view texture
        LOG_FIREHOSE("%s: getting view %d for stereo mode %d", Q_FUNC_INFO, v, int(stereoMode));
        QMatrix4x4 projectionMatrix;
        QMatrix4x4 viewMatrix;
        if (_bino->assumeThreeSixtyMode()) {
            float verticalVieldOfView = qDegreesToRadians(50.0f);
            float aspectRatio = float(_width) / _height;
            float top = qTan(verticalVieldOfView * 0.5f);
            float bottom = -top;
            float right = top * aspectRatio;
            float left = -right;
            projectionMatrix.frustum(left, right, bottom, top, 1.0f, 100.0f);
            QQuaternion rotation = QQuaternion::fromEulerAngles(
                    -(_threeSixtyVerticalAngleBase + _threeSixtyVerticalAngleCurrent),
                    -(_threeSixtyHorizontalAngleBase + _threeSixtyHorizontalAngleCurrent), 0.0f);
            viewMatrix.rotate(rotation);
        }
        _bino->render(projectionMatrix, viewMatrix, v, viewWidth, viewHeight, _viewTex[v]);
        // generate mipmaps for the view texture
        glBindTexture(GL_TEXTURE_2D, _viewTex[v]);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    // Put the views on screen in the current mode
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, _width, _height);
    glDisable(GL_DEPTH_TEST);
    float relWidth = 1.0f;
    float relHeight = 1.0f;
    float screenAspectRatio = _width / float(_height);
    if (screenAspectRatio < frameDisplayAspectRatio)
        relHeight = screenAspectRatio / frameDisplayAspectRatio;
    else
        relWidth = frameDisplayAspectRatio / screenAspectRatio;
    glUseProgram(_prg.programId());
    _prg.setUniformValue("view0", 0);
    _prg.setUniformValue("view1", 1);
    _prg.setUniformValue("relativeWidth", relWidth);
    _prg.setUniformValue("relativeHeight", relHeight);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _viewTex[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, _viewTex[1]);
    glBindVertexArray(_quadVao);
    if (_openGLStereo) {
        LOG_FIREHOSE("oglstereo draw");
        if (stereoMode == Mode_OpenGL_Stereo) {
            glDrawBuffer(GL_BACK_LEFT);
            _prg.setUniformValue("stereoMode", static_cast<int>(Mode_Left));
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glDrawBuffer(GL_BACK_RIGHT);
            _prg.setUniformValue("stereoMode", static_cast<int>(Mode_Right));
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
        } else {
            if (stereoMode == Mode_Alternating)
                stereoMode = (_alternatingLastView == 0 ? Mode_Right : Mode_Left);
            _prg.setUniformValue("stereoMode", static_cast<int>(stereoMode));
            glDrawBuffer(GL_BACK_LEFT);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            glDrawBuffer(GL_BACK_RIGHT);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
        }
    } else {
        LOG_FIREHOSE("normal draw");
        if (stereoMode == Mode_Alternating)
            stereoMode = (_alternatingLastView == 0 ? Mode_Right : Mode_Left);
        _prg.setUniformValue("stereoMode", static_cast<int>(stereoMode));
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    }

    // Update Mode_Alternating
    if (_stereoMode == Mode_Alternating && frameIsStereo) {
        _alternatingLastView = (_alternatingLastView == 0 ? 1 : 0);
        update();
    }
}

void Widget::resizeGL(int w, int h)
{
    _width = w;
    _height = h;
}

void Widget::keyPressEvent(QKeyEvent* e)
{
    _bino->keyPressEvent(e);
}

void Widget::mousePressEvent(QMouseEvent* e)
{
    _inThreeSixtyMovement = true;
    _threeSixtyMovementStart = e->position();
    _threeSixtyHorizontalAngleCurrent = 0.0f;
    _threeSixtyVerticalAngleCurrent = 0.0f;
}

void Widget::mouseReleaseEvent(QMouseEvent*)
{
    _inThreeSixtyMovement = false;
    _threeSixtyHorizontalAngleBase += _threeSixtyHorizontalAngleCurrent;
    _threeSixtyVerticalAngleBase += _threeSixtyVerticalAngleCurrent;
    _threeSixtyHorizontalAngleCurrent = 0.0f;
    _threeSixtyVerticalAngleCurrent = 0.0f;
}

void Widget::mouseMoveEvent(QMouseEvent* e)
{
    if (_inThreeSixtyMovement) {
        // position delta
        QPointF posDelta = e->position() - _threeSixtyMovementStart;
        // horizontal angle delta
        float dx = posDelta.x();
        float xf = dx / _width; // in [-1,+1]
        _threeSixtyHorizontalAngleCurrent = -xf * 180.0f;
        // vertical angle
        float dy = posDelta.y();
        float yf = dy / _height; // in [-1,+1]
        _threeSixtyVerticalAngleCurrent = -yf * 90.0f;
        update();
    }
}

void Widget::mediaChanged(PlaylistEntry)
{
    _inThreeSixtyMovement = false;
    _threeSixtyHorizontalAngleBase = 0.0f;
    _threeSixtyVerticalAngleBase = 0.0f;
    _threeSixtyHorizontalAngleCurrent = 0.0f;
    _threeSixtyVerticalAngleCurrent = 0.0f;
}
