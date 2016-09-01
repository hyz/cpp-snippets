//
// VideoRender.cpp
// Created by WANGBIAO on 2013-9-4
// Copyright (c) 2013. SHENZHEN HUAZHEN Electronics Co.Ltd. All rights reserved.
//
#include <errno.h>
#include <android/log.h>
#if 0 //ndef BUILD_RELEASE
#  include <opencv/highgui.h>
#  include <opencv/cv.h>
#endif
#include "VideoRender.h"

// #include "Utils/log.h"
#define LOG_TAG    "HGSR"
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGV(...)  __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

VideoRender::VideoRender()
: mWindow(0)
, mWindowWidth(0)
, mWindowHeight(0)
, mCardWidth(0)
, mCardHeight(0)
, mResultLen(0)
, mPosRevs(-1)
, mPosCut(-1)
{
	LOGV("%s [i]", __func__);
	pthread_mutex_init(&mMutex, NULL);
	memset(&mCardImage, 0 ,sizeof(mCardImage));
	memset(&mResult, 0 ,sizeof(mResult));

#if 0 //ndef BUILD_RELEASE
	IplImage *pCard = 0;

	pCard = cvLoadImage(CARDS_RES_FILE, -1);

	if(pCard)
	{
		mCardImage.width   = pCard->width;
		mCardImage.height  = pCard->height;
		mCardImage.stride  = mCardImage.width*4;
		mCardImage.format  = IMAGEFORMATE_RGBA_8888;
		mCardImage.size    = mCardImage.height*mCardImage.stride;
		mCardImage.porg    = new uint8_t[mCardImage.size+16];
		mCardImage.pdata   = (uint8_t*)(((uint32_t)mCardImage.porg+15)&(~15));

		IplImage* pRGBA = cvCreateImageHeader(cvSize(mCardImage.width, mCardImage.height),IPL_DEPTH_8U,4);
		cvSetData(pRGBA, mCardImage.pdata, mCardImage.stride);
		cvCvtColor(pCard, pRGBA, CV_BGR2RGBA);
		cvReleaseImage(&pCard);
		cvReleaseImageHeader(&pRGBA);

		mCardWidth  = mCardImage.width / CARDS_PER_ROW;
		mCardHeight = mCardImage.height/ CARDS_PER_COL;

		//LOGI("Card %dx%d=>%dx%d\n",mCardImage.width,mCardImage.height,mCardWidth,mCardHeight);
	}
	else
		LOGE("Card res not found %s\n", CARDS_RES_FILE);
#endif

	LOGV("%s [o]", __func__);
}

VideoRender::~VideoRender()
{
	pthread_mutex_destroy(&mMutex);
	if(mWindow!=0) ANativeWindow_release(mWindow);
	mWindow = 0;
	if(mCardImage.porg) delete[] mCardImage.porg; mCardImage.porg = 0;
}

int VideoRender::setWindow(ANativeWindow* window)
{
	LOGV("%s [i]", __func__);
	pthread_mutex_lock(&mMutex);

	if(mWindow!=0) ANativeWindow_release(mWindow);
	mWindow = window;

	if(mWindow!=0)
	{
		ANativeWindow_Buffer buffer;

		/*if(ANativeWindow_lock(mWindow, &buffer, NULL)==0)
		{
			if(buffer.format == WINDOW_FORMAT_RGBA_8888 || buffer.format == WINDOW_FORMAT_RGBX_8888)
				memset(buffer.bits, 0xFF, buffer.height*buffer.stride*4);
			else if(buffer.format == WINDOW_FORMAT_RGB_565)
				memset(buffer.bits, 0xFF, buffer.height*buffer.stride*2);

			ANativeWindow_unlockAndPost(mWindow);
		}*/
	}

	pthread_mutex_unlock(&mMutex);

	LOGV("%s [o]", __func__);
}

int VideoRender::renderFrame(Image* p, bool otherThread)
{
	int scaleType = RENDER_SCALE_NONE;
	int ret = 0, dstw=0, dsth=0,srcw=0,srch=0, minw=0, minh=0;
	uint8_t  *pdst = 0, *psrc=0;
	ANativeWindow_Buffer buffer;

	if(!p) return 0;

	pthread_mutex_lock(&mMutex);

	if(mWindow==0 || p==0){
		LOGE("no render windows\n");
		goto _EXIT_MUTEX_UNLOCK;
	}

	ret = ANativeWindow_lock(mWindow, &buffer, NULL);

	if(ret !=0 ){
		LOGE("%s: lock windows fail. errno: %s,ret = %d\n", __func__, strerror(errno), ret);
		goto _EXIT_MUTEX_UNLOCK;
	}

	if(!(buffer.format == WINDOW_FORMAT_RGBA_8888 || buffer.format == WINDOW_FORMAT_RGBX_8888)){
		LOGE("unsupported format %d\n",buffer.format);
		goto _EXIT_WINDOW_UNLOCK;
	}

	pdst = ((uint8_t*)buffer.bits) ;
	psrc = p->pdata;
	dstw = buffer.width;
	dsth = buffer.height;


	if(p->width < 600)
		scaleType = RENDER_SCALE_NONE;
	else if(p->width <= 800)
		scaleType = RENDER_SCALE_3P4;
	else if(p->width<=1280)
        scaleType = RENDER_SCALE_1P2;
	else if(p->width<=1600)
		scaleType = RENDER_SCALE_1P3;

	switch(scaleType)
	{
	case RENDER_SCALE_3P4:
	{
		srcw = p->width*3/4;
		srch = p->height*3/4;

		int offset = (dstw-srcw)/2;

		if(offset>0){
			pdst = pdst + offset*4;
			minw = p->width;
		}
		else{
			psrc = psrc - offset;
			minw = dstw*4/3;
		}

		if(dsth>srch)minh = p->height;
		else minh = dsth*4/3;

		//LOGI("3/4 %dx%d : %dx%d => %dx%d offset %d\n",p->width, p->height, buffer.width, buffer.height, minw, minh, offset);
		renderto4of3RGBX((void*)psrc, p->stride, (void*)pdst, buffer.stride*4, minw, minh);
	}
	break;
	case RENDER_SCALE_1P3:
	{
		srcw = p->width/3;
		srch = p->height/3;

		int offset = (dstw-srcw)/2;

		if(offset>0){
			pdst = pdst + offset*4;
			minw = p->width;
		}
		else{
			psrc = psrc - offset;
			minw = dstw*3;
		}

		if(dsth>srch) minh = p->height;
		else minh = dsth*3;

		//LOGI("1/3 %dx%d : %dx%d => %dx%d offset %d\n",p->width, p->height, buffer.width, buffer.height, minw, minh, offset);
		renderto3of1RGBX((void*)psrc, p->stride, (void*)pdst, buffer.stride*4, minw, minh);
	}
	break;
	case RENDER_SCALE_1P2:
	{
		srcw = p->width/2;
		srch = p->height/2;

		int offset = (dstw-srcw)/2;

		if(offset>0){
			pdst = pdst + offset*4;
			minw = p->width;
		}
		else{
			psrc = psrc - offset;
			minw = dstw*2;
		}

		if(dsth>srch) minh = p->height;
		else minh = dsth*2;

		//LOGI("1/3 %dx%d : %dx%d => %dx%d offset %d\n",p->width, p->height, buffer.width, buffer.height, minw, minh, offset);
		renderto2of1RGBX((void*)psrc, p->stride, (void*)pdst, buffer.stride*4, minw, minh);
	}
    break;
	default:
	{
		srcw = p->width;
		srch = p->height;

		int offset = (dstw-srcw)/2;

		if(offset>0){
			pdst = pdst + offset*4;
			minw = p->width;
		}
		else{
			psrc = psrc - offset;
			minw = dstw;
		}

		if(dsth>srch) minh = p->height;
		else minh = dsth;

		//LOGI("1/1 %dx%d : %dx%d => %dx%d offset %d\n",p->width, p->height, buffer.width, buffer.height, minw, minh, offset);
		renderGraytoRGBX((void*)psrc, p->stride, (void*)pdst, buffer.stride*4, minw, minh);
		if(buffer.height>p->height)
		{
			uint8_t* pstart = ((uint8_t*)buffer.bits)+p->height*buffer.stride*4;
			int      msize  = (buffer.height-p->height)*buffer.stride*4;
			memset(pstart, 0, msize);
		}
	}
	break;
	}

	//if(mResultLen)
	//	renderResulttoRGBX(mResult, mResultLen, pdst, buffer.stride*4, dstw, dsth);

_EXIT_WINDOW_UNLOCK:
	ANativeWindow_unlockAndPost(mWindow);

_EXIT_MUTEX_UNLOCK:
	pthread_mutex_unlock(&mMutex);

	return 0;
}

int VideoRender::renderGraytoRGBX(void* in, int stridein, void* out, int strideout, int w, int h)
{
	uint32_t* src_row = (uint32_t*)in;
	uint32_t* dst_row = (uint32_t*)out;
	uint32_t  *src, *dst;

	for(int y=0; y<h; y++)
	{
		src = src_row;
		dst = dst_row;

		for(int x=0; x<w; x+=4)
		{
			uint32_t v, v1, v2, v3, v4;

			v  = *src;src++;
			v1 = (v    )&0xFF;
			v2 = (v>>8 )&0xFF;
			v3 = (v>>16)&0xFF;
			v4 = (v>>24)&0xFF;

			*(dst+0) = (v1<<16)|(v1<<8)|(v1);
			*(dst+1) = (v2<<16)|(v2<<8)|(v2);
			*(dst+2) = (v3<<16)|(v3<<8)|(v3);
			*(dst+3) = (v4<<16)|(v4<<8)|(v4);

			dst +=4;
		}

		src_row = (uint32_t*)(((uint8_t*)src_row) + stridein);
		dst_row = (uint32_t*)(((uint8_t*)dst_row) + strideout);
	}
}

int VideoRender::renderto4of3RGBX(void* in, int stridein, void* out, int strideout, int w, int h)
{
	uint32_t* src_row = (uint32_t*)in;
	uint32_t* dst_row = (uint32_t*)out;
	uint32_t  *src, *dst;

	for(int y=0; y<h; y++)
	{
		src = src_row;
		dst = dst_row;

		if((y&0x3) != 0)
		{
			for(int x=0; x<w; x+=4)
			{
				uint32_t v, v1, v2, v3, v4;

				v  = *src;src++;
				v1 = (v    )&0xFF;
				v2 = (v>>8 )&0xFF;
				v3 = (v>>16)&0xFF;
				v4 = (v>>24)&0xFF;

				*(dst+0) = (v1<<16)|(v1<<8)|(v1);
				*(dst+1) = (v2<<16)|(v2<<8)|(v2);
				*(dst+2) = (v3<<16)|(v3<<8)|(v3);
				dst +=3;
			}

			src_row = (uint32_t*)(((uint8_t*)src_row) + stridein);
			dst_row = (uint32_t*)(((uint8_t*)dst_row) + strideout);

		}
		else
		{
			src_row = (uint32_t*)(((uint8_t*)src_row) + stridein);
		}

	}
}

int VideoRender::renderto3of1RGBX(void* in, int stridein, void* out, int strideout, int w, int h)
{
	uint8_t* src_row = (uint8_t*)in;
	uint8_t* dst_row = (uint8_t*)out;
	uint8_t  *src, *dst;

	h = (h/3)*3;
	w = (w/3)*3;

	for(int y=0; y<h; y+=3)
	{
		src = src_row;
		dst = dst_row;

		for(int x=0; x<w; x+=3)
		{
			uint8_t v = *src; src +=3;
			*((uint32_t*)dst) = (v<<16)|(v<<8)|(v);
			dst +=4;
		}

		src_row = src_row + stridein*3;
		dst_row = dst_row + strideout;
	}
}

int VideoRender::renderto2of1RGBX(void* in, int stridein, void* out, int strideout, int w, int h)
{
	uint8_t* src_row = (uint8_t*)in;
	uint8_t* dst_row = (uint8_t*)out;
	uint8_t  *src, *dst;

	//h = (h/3)*3; w = (w/3)*3;

	for(int y=0; y<h; y+=2)
	{
		src = src_row;
		dst = dst_row;

		for(int x=0; x<w; x+=2)
		{
			uint32_t v = *src; src +=2;
			*((uint32_t*)dst) = (v<<16)|(v<<8)|(v);
			dst +=4;
		}

		src_row = src_row + stridein*2;
		dst_row = dst_row + strideout;
	}
}

int VideoRender::setResult(int* p, int len)
{
	if(p==0) return -1;

	pthread_mutex_lock(&mMutex);

	mPosCut  = p[len];
	mPosRevs = p[len+1];

	len = len > 162 ? 162: len;
	mResultLen = len;
	memcpy(mResult, p, len*sizeof(int));

	pthread_mutex_unlock(&mMutex);
	return 0;
}

int VideoRender::copyOneCard(void* in, int stridein,void* out, int strideout, int w, int h)
{
	uint8_t *dst, *src;

	src = (uint8_t*)in;
	dst = (uint8_t*)out;

	for(int i=0; i<h; i++)
	{
		memcpy(dst, src, w*4);
		src += stridein;
		dst += strideout;
	}

	return 0;
}

int VideoRender::renderResulttoRGBX(int* in, int len, void* out, int strideout, int w, int h)
{
	if(mCardImage.pdata==0) return -1;
	int wcount = w /mCardWidth;
	int round  = len/wcount;
	int remain = len - round*wcount;
	int pc = 0;

	for(int i=0; i<round; i++)
	{
		uint8_t* row = ( (uint8_t*)out + strideout*mCardHeight*i);

		for(int j=0; j<wcount; j++)
		{
			int v = in[pc];
			v = v<0  ? MAX_CARDS_TYPE : v;
			v = v>MAX_CARDS_TYPE ? MAX_CARDS_TYPE : v;

			int y    = v/13;
			int x    = v-y*13;

			uint8_t* src = mCardImage.pdata + y*mCardImage.stride*mCardHeight + x*4*mCardWidth;
			copyOneCard(src, mCardImage.stride, row, strideout, mCardWidth, mCardHeight);
			if(pc == mPosRevs || pc == mPosCut)rectangleCard(row, strideout, mCardWidth, mCardHeight, 0xFF00FF00);
			row += mCardWidth*4;
			pc++;
		}
	}

	uint8_t* row = ((uint8_t*)out + strideout*mCardHeight*round);
	if(remain)memset(row, 0, strideout*mCardHeight);

	for(int i=0; i<remain; i++)
	{
		int v = in[pc];
		v = v<0  ? MAX_CARDS_TYPE : v;
		v = v>MAX_CARDS_TYPE ? MAX_CARDS_TYPE : v;

		int y    = v/13;
		int x    = v-y*13;

		uint8_t* src = mCardImage.pdata + y*mCardImage.stride*mCardHeight + x*4*mCardWidth;
		copyOneCard(src, mCardImage.stride, row, strideout, mCardWidth, mCardHeight);
		if(pc == mPosRevs || pc == mPosCut) rectangleCard(row, strideout, mCardWidth, mCardHeight, 0xFF00FF00);
		row += mCardWidth*4;
		pc++;

	}

	return (round+!!remain)*mCardHeight;
}

int VideoRender::rectangleCard(void* src, int stride, int w, int h, unsigned int v)
{
	uint32_t* psrc = (uint32_t*) src;
	stride = stride/4;

	for(int i=0; i<w; i++)
	{
		psrc[i]               = v;
		psrc[i+stride*(h-1)]  = v;
	}

	for(int i=0; i<h; i++)
	{
		psrc[i*stride]        = v;
		psrc[i*stride+(w-1)]  = v;
	}

	return 0;
}



///////////////////////////////////////////////////////////////////////////////////////
VideoRenderGL::VideoRenderGL()
:VideoRender()
,mSurfaceW(0)
,mSurfaceH(0)
,mIsWndChange(false)
,mIsGLInit(false)
,mSurface(0)
,mContext(0)
,mDisplay(0)
,mIsCopyed(false)
,mpRenderR(0)
{
	memset(&mLocalImg, 0, sizeof(mLocalImg));
	memset(&mRstImg, 0 , sizeof(mRstImg));

   mpRenderY  = new RenderYUV(0);

#ifndef BUILD_RELEASE
   //mpRenderR  = new RenderRst(1);
#endif

}

VideoRenderGL::~VideoRenderGL()
{
	LOGV("%s [i]", __func__);
	if(mIsGLInit) releaseGL();

	if(mLocalImg.porg) delete[] mLocalImg.porg; mLocalImg.porg = 0;

	if(mpRenderY)  delete mpRenderY; mpRenderY=0;
	if(mpRenderR)  delete mpRenderR; mpRenderR=0;
	LOGV("%s [o]", __func__);

}

int VideoRenderGL::renderFrame(Image* p, bool otherThread)
{

	if(otherThread && p)
	{
		//LOGI("*********copy frame****************\n");
		return copyToLocalImg(p);
	}

	pthread_mutex_lock(&mMutex);

	//LOGI("is init %d window 0x%x change %d\n", mIsGLInit, mWindow, mIsWndChange);

	if(mIsGLInit && mIsWndChange)
	{
		releaseGL();//native windows change
		mIsWndChange = false;
	}

	if(!mIsGLInit && mWindow)
	{
		mIsGLInit = initGL() >= 0;
	}

	if(!p && mIsCopyed)
	{
		p = &mLocalImg;
		mIsCopyed = false;
	}

	if(!mIsGLInit || !p)
	{
		pthread_mutex_unlock(&mMutex);
		return -1;
	}

	if(mpRenderY) mpRenderY->render(p);
	if(mpRenderR && mResultLen &&mRstImg.pdata)
	{
		mRstImg.flags = renderResulttoRGBX(mResult, mResultLen, mRstImg.pdata, mRstImg.stride, mRstImg.width, mRstImg.height);
		mpRenderR->render(&mRstImg);
	}

    eglSwapBuffers(mDisplay, mSurface);

    pthread_mutex_unlock(&mMutex);

    return 0;
}

int  VideoRenderGL::copyToLocalImg(Image* p)
{
	//copy the frame from other thread
	if(mLocalImg.size != p->size)
	{
		if(mLocalImg.porg) delete[] mLocalImg.porg; mLocalImg.porg = 0;

		mLocalImg         = *p;
		mLocalImg.porg    = new uint8_t[mLocalImg.size+16];
		mLocalImg.pdata   = (uint8_t*)(((ptrdiff_t)mLocalImg.porg+15)&(~15));
	}

	memcpy(mLocalImg.pdata, p->pdata, p->size);
	mIsCopyed = true;
	return 0;
}

int VideoRenderGL::setWindow(ANativeWindow* window)
{
	if(!window)mIsWndChange = true;
	VideoRender::setWindow(window);
}

int VideoRenderGL::initGL()
{

	const EGLint attribs[] =
	{
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	    EGL_BLUE_SIZE, 8,
	    EGL_GREEN_SIZE, 8,
	    EGL_RED_SIZE, 8,
	    EGL_ALPHA_SIZE,  8,
	    EGL_NONE
	 };

	const EGLint contextattribs[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	EGLDisplay display;
	EGLConfig config;
	EGLint numConfigs;
	EGLint format;
	EGLSurface surface;
	EGLContext context;
	EGLint width;
	EGLint height;
	GLfloat ratio;

	LOGI("****Initializing OpenGL context*****");

	if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
	    LOGE("eglGetDisplay() returned error %d", eglGetError());
	    return -1;
	}

	if (!eglInitialize(display, 0, 0)) {
		LOGE("eglInitialize() returned error %d", eglGetError());
	    return -1;
	}

	if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs)) {
		LOGE("eglChooseConfig() returned error %d", eglGetError());
	    return -1;
	}

	if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
		LOGE("eglGetConfigAttrib() returned error %d", eglGetError());
	    return -1;
	}

	    //ANativeWindow_setBuffersGeometry(mWindow, 0, 0, format);

	if (!(surface = eglCreateWindowSurface(display, config, mWindow, 0))) {
	    LOGE("eglCreateWindowSurface() returned error %d", eglGetError());
	    return -1;
	}

	if (!(context = eglCreateContext(display, config, 0, contextattribs))) {
	    LOGE("eglCreateContext() returned error %d", eglGetError());
	    return -1;
	}

	if (!eglMakeCurrent(display, surface, surface, context)) {
	    LOGE("eglMakeCurrent() returned error %d", eglGetError());
	    return -1;
	}

	if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
	    !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
	    LOGE("eglQuerySurface() returned error %d", eglGetError());
	    return -1;
	}

	mDisplay = display;
	mSurface = surface;
	mContext = context;
	mSurfaceW= width;
	mSurfaceH= height;

	if(mpRenderY)mpRenderY->init();
	if(mpRenderR)mpRenderR->init();

	glViewport(0, 0, width, height);

	if(mpRenderR)initRstImg(width, height);

	return 0;
}

int VideoRenderGL::releaseGL()
{
	LOGD("%s [i]", __func__);

	if(mpRenderY)mpRenderY->release();
	if(mpRenderR)mpRenderR->release();

	if(mRstImg.porg) delete[]  mRstImg.porg;  mRstImg.porg = 0;

    eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(mDisplay, mContext);
    eglDestroySurface(mDisplay, mSurface);
    eglTerminate(mDisplay);

    mDisplay = EGL_NO_DISPLAY;
    mSurface = EGL_NO_SURFACE;
    mContext = EGL_NO_CONTEXT;

    mIsGLInit = false;
    LOGD("%s [o]", __func__);
	return 0;
}

int  VideoRenderGL::initRstImg(int w, int h)
{
	if(mRstImg.porg) delete[]  mRstImg.porg;  mRstImg.porg = 0;

	mRstImg.format = IMAGEFORMATE_RGBA_8888;
	mRstImg.width  = w;
	mRstImg.stride = mRstImg.width*4;
	mRstImg.height = h;
	mRstImg.size   = mRstImg.stride*mRstImg.height;
	mRstImg.porg   = new uint8_t[mRstImg.size+16];
	mRstImg.pdata  = (uint8_t*)(((ptrdiff_t)mRstImg.porg+15)&(~15));

	return 0;
}


////////////////////////////////////////////////////////////////////////////////////

static const char gVertexShaderString[] = SHADER_STRING
(
 attribute vec4 position;
 attribute vec4 inTexCoord;

 varying highp vec2 texcoord;
 void main()
 {
     gl_Position = position;
     texcoord  = inTexCoord.xy;
 }
);

static const char gYUV420RGBFragmentShaderString[] = SHADER_STRING
(
 varying highp vec2 texcoord;
 uniform sampler2D texY;

 void main()
 {
	 mediump vec3 r;
	 r.rgb = texture2D(texY, texcoord).rrr;
     gl_FragColor = vec4(r,1.0);
 }
);

static const char gRGBFragmentShaderString[] = SHADER_STRING
(
 varying highp vec2 texcoord;
 uniform sampler2D texY;

 void main()
 {
	 mediump vec3 r;
	 r.rgb = texture2D(texY, texcoord).rgb;
     gl_FragColor = vec4(r, 1.0);
 }
);

RenderYUV::RenderYUV(int idx)
:ShaderUnit(idx)
{

}

RenderYUV::~RenderYUV()
{

}

int  RenderYUV::init()
{
	mvShaderString = gVertexShaderString;
	mfShaderString = gYUV420RGBFragmentShaderString;
	ShaderUnit::init();
}

int RenderYUV::release()
{
	ShaderUnit::release();
}

int RenderYUV::render(Image* p)
{
	glUseProgram(mProgram);
	checkGlError("glUseProgram");

	/*glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	checkGlError("glClearColor");
	glClear(GL_COLOR_BUFFER_BIT);
	checkGlError("glClear");*/

	glActiveTexture(GL_TEXTURE0+mIdx);
	checkGlError("glActiveTexture");
	glBindTexture(GL_TEXTURE_2D, mTexture);
	checkGlError("glBindTexture");
	setTexParameter();

    // LOGD("glTexImage2D Image: stride %u, height %u, %p", p->stride, p->height, p->pdata);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, p->stride, p->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, p->pdata);
	checkGlError("glTexImage2D");

	glUniform1i(muTexid, mIdx);
	checkGlError("glUniform1i");

	glVertexAttribPointer(mvTexcoord, 2, GL_FLOAT, GL_FALSE, 0, mtexcoord);
	checkGlError("glVertexAttribPointer");
	glEnableVertexAttribArray(mvTexcoord);
	checkGlError("glEnableVertexAttribArray");

	glVertexAttribPointer(mvPoshandle, 2, GL_FLOAT, GL_FALSE, 0, mvertices);
	checkGlError("glVertexAttribPointer");
	glEnableVertexAttribArray(mvPoshandle);
	checkGlError("glEnableVertexAttribArray");

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	checkGlError("glDrawArrays");
	return 0;
}

//////////////////////////////////////////////////////////////
RenderRst::RenderRst(int idx)
:ShaderUnit(idx)
{
}

RenderRst::~RenderRst()
{

}

int  RenderRst::init()
{
	mvShaderString = gVertexShaderString;
	mfShaderString = gRGBFragmentShaderString;
	ShaderUnit::init();
}

int RenderRst::release()
{
	ShaderUnit::release();
}

int RenderRst::render(Image* p)
{
	glUseProgram(mProgram);
	checkGlError("glUseProgram");

	glActiveTexture(GL_TEXTURE0+mIdx);
	checkGlError("glActiveTexture");
	glBindTexture(GL_TEXTURE_2D, mTexture);
	checkGlError("glBindTexture");
	setTexParameter();

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p->width, p->flags, 0, GL_RGBA, GL_UNSIGNED_BYTE, p->pdata);
	checkGlError("glTexImage2D");

	glUniform1i(muTexid, mIdx);
	checkGlError("glUniform1i");

	float rat = -((float)p->flags/p->height - 0.5f)*2.0f;

	mvertices[1] = rat;
	mvertices[3] = rat;

	glVertexAttribPointer(mvTexcoord, 2, GL_FLOAT, GL_FALSE, 0, mtexcoord);
	checkGlError("glVertexAttribPointer");
	glEnableVertexAttribArray(mvTexcoord);
	checkGlError("glEnableVertexAttribArray");

	glVertexAttribPointer(mvPoshandle, 2, GL_FLOAT, GL_FALSE, 0, mvertices);
	checkGlError("glVertexAttribPointer");
	glEnableVertexAttribArray(mvPoshandle);
	checkGlError("glEnableVertexAttribArray");

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	checkGlError("glDrawArrays");
	return 0;
}

static const GLfloat gVertices[] =
{
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
};

static const GLfloat gTexCoord[] = {
    0.0f, 1.0f,
    1.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 0.0f,
};

//////////////////////////////////////////////////////////////////////////////////
ShaderUnit::ShaderUnit(int idx)
:mIdx(idx)
,mvShaderString(0)
,mfShaderString(0)
,mProgram(0)
,mvertices(0)
,mtexcoord(0)
,mvPoshandle(-1)
,mvTexcoord(-1)
,muTexid(-1)
,mTexture(-1)
{
	mvertices = new GLfloat[8];
    mtexcoord = new GLfloat[8];

    memcpy(mvertices, gVertices, sizeof(gVertices));
    memcpy(mtexcoord, gTexCoord, sizeof(gTexCoord));
}

ShaderUnit::~ShaderUnit()
{
	if(mvertices) delete[] mvertices; mvertices = 0;
	if(mtexcoord) delete[] mtexcoord; mtexcoord = 0;
}

int ShaderUnit::init()
{
	//printGLString("Version", GL_VERSION);
	//printGLString("Vendor", GL_VENDOR);
	//printGLString("Renderer", GL_RENDERER);
	//printGLString("Extensions", GL_EXTENSIONS);

	LOGI("setupShaderUnit");
	mProgram = createProgram(mvShaderString, mfShaderString);
	if (!mProgram) {
		LOGE("Could not create program.");
	    return -1;
	}

	mvPoshandle = glGetAttribLocation(mProgram, "position");
	checkGlError("glGetAttribLocation");
	LOGI("glGetAttribLocation(\"position\") = %d\n",mvPoshandle);

	mvTexcoord = glGetAttribLocation(mProgram, "inTexCoord");
	checkGlError("glGetAttribLocation");
	LOGI("glGetAttribLocation(\"inTexCoord\") = %d\n",mvTexcoord);

	muTexid = glGetUniformLocation(mProgram, "texY");
	checkGlError("glGetUniformLocation");
	LOGI("glGetUniformLocation %d\n",muTexid);

	glGenTextures(1, &mTexture);
	checkGlError("glGenTextures");
	LOGI("glGenTextures %d\n", mTexture);

	return 0;
}

int ShaderUnit::release()
{
	if(mTexture>=0) glDeleteTextures(1, &mTexture); mTexture= -1;
	return 0;
}

int ShaderUnit::render(Image* p)
{
	return 0;
}


void ShaderUnit::setTexParameter(GLint filter, GLint warp)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    checkGlError("glTexParameteri");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    checkGlError("glTexParameteri");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, warp);
    checkGlError("glTexParameteri");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, warp);
    checkGlError("glTexParameteri");
}

void ShaderUnit::printGLString(const char *name, GLenum s)
{
   const char *v = (const char *) glGetString(s);
   LOGI("GL %s = %s\n", name, v);
}

void ShaderUnit::checkGlError(const char* op)
{
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        LOGI("after %s() glError (0x%x)\n", op, error);
    }
}

GLuint ShaderUnit::loadShader(GLenum shaderType, const char* pSource)
{
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    LOGE("Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint ShaderUnit::createProgram(const char* pVertexSource, const char* pFragmentSource)
{
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        glLinkProgram(program);
        checkGlError("glLinkProgram");
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);

        LOGI("link Status %d\n", linkStatus);

        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    LOGE("Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }

            glDeleteProgram(program);
            program = 0;
        }
    }

    if (vertexShader)
        glDeleteShader(vertexShader);

    if (pixelShader)
        glDeleteShader(pixelShader);

    return program;
}

