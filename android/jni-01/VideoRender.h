//
// VideoRender.h
// Created by WANGBIAO on 2013-9-4
// Copyright (c) 2013. SHENZHEN HUAZHEN Electronics Co.Ltd. All rights reserved.
//

#ifndef RENDER_H_
#define RENDER_H_

#include <pthread.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <android/native_window.h>
#include "Include/Global.h"

#define  CARDS_PER_ROW       13
#define  CARDS_PER_COL       5
#define  CARDS_RES_FILE      "/data/data/com.huazhen.barcode/raw/poker.png"

enum ScaleType
{
	RENDER_SCALE_NONE = 0,
	RENDER_SCALE_3P4 = 1,
	RENDER_SCALE_1P2 = 2,
	RENDER_SCALE_1P3 = 3,
};

class VideoRender
{
public:
	VideoRender();
	virtual ~VideoRender();
	virtual int renderFrame(Image* p, bool otherThread = false);
	int setResult(int* p, int len);
	virtual int setWindow(ANativeWindow* window);

protected:
	int renderGraytoRGBX(void* in, int stridein,void* out, int strideout, int w, int h);
	int renderto4of3RGBX(void* in, int stridein,void* out, int strideout, int w, int h);
	int renderto3of1RGBX(void* in, int stridein,void* out, int strideout, int w, int h);
int renderto2of1RGBX(void* in, int stridein, void* out, int strideout, int w, int h);
	int renderResulttoRGBX(int* in, int len, void* out, int strideout, int w, int h);
	int copyOneCard(void* in, int stridein,void* out, int strideout, int w, int h);
	int rectangleCard(void* src, int stride, int w, int h, unsigned int v);

protected:
	ANativeWindow*      mWindow;
	int                 mWindowWidth;
	int                 mWindowHeight;
	Image               mCardImage;

	int                 mCardWidth;
	int                 mCardHeight;
	pthread_mutex_t     mMutex;

	int                 mPosRevs;
	int                 mPosCut;
	int                 mResultLen;
	int                 mResult[MAX_RESULT_CARDS];
};


#define STRINGIZE(x) #x
#define SHADER_STRING(x) STRINGIZE(x)

class ShaderUnit
{
public:

	ShaderUnit(int idx);
	virtual ~ShaderUnit();
	virtual int  init();
	virtual int  release();
	virtual int  render(Image* p);
protected:

	void setTexParameter(GLint filter=GL_LINEAR, GLint warp=GL_CLAMP_TO_EDGE);
	static GLuint createProgram(const char* pVertexSource, const char* pFragmentSource);
	static GLuint loadShader(GLenum shaderType, const char* pSource);
	static void printGLString(const char *name, GLenum s);
	static void checkGlError(const char* op);

protected:
	int                 mIdx;

	const char*         mvShaderString;
	const char*         mfShaderString;
    GLuint              mProgram;
    GLuint              mvPoshandle;
    GLuint              mvTexcoord;
    GLuint              muTexid;
    GLuint              mTexture;

    GLfloat*            mvertices;
    GLfloat*            mtexcoord;
};

class RenderYUV: public ShaderUnit
{
public:
	RenderYUV(int idx);
	virtual~ RenderYUV();

	virtual int  init();
	virtual int  release();
	virtual int  render(Image* p);
protected:
};

class RenderRst: public ShaderUnit
{
public:
	RenderRst(int idx);
	virtual~ RenderRst();

	virtual int  init();
	virtual int  release();
	virtual int  render(Image* p);
protected:
};

class VideoRenderGL: public VideoRender
{
public:
	VideoRenderGL();
	virtual ~VideoRenderGL();
	virtual int renderFrame(Image* p, bool otherThread = false);

	virtual int setWindow(ANativeWindow* window);

protected:

	int  initGL();
	int  releaseGL();
	int  copyToLocalImg(Image* p);
	int  initRstImg(int w, int h);

protected:

	Image               mLocalImg;
	Image               mRstImg;
	int                 mSurfaceW;
	int                 mSurfaceH;
	bool                mIsCopyed;
	bool                mIsGLInit;
	bool                mIsWndChange;
    EGLSurface          mSurface;
    EGLContext          mContext;
    EGLDisplay          mDisplay;


    ShaderUnit*         mpRenderY;
    ShaderUnit*         mpRenderR;

};


#endif /* RENDER_H_ */
