/// http://blog.csdn.net/silangquan/article/details/14442895
#include <iostream>  
#include <SDL2/SDL.h>    
#include <GL/gl.h>    
#include <GL/glu.h>    
using namespace std;  
  
const int SCREEN_WIDTH = 800;    
const int SCREEN_HEIGHT =600;   
SDL_Window *mainwindow; /* Our window handle */  
SDL_GLContext maincontext; /* Our opengl context handle */  
  
/* A simple function that prints a message, the error code returned by SDL, 
 * and quits the application */  
void sdldie(const char *msg)  
{  
    printf("%s: %s\n", msg, SDL_GetError());  
    SDL_Quit();  
    exit(1);  
}  
  
void quit( int code )    
{    
    SDL_Quit( );    
    /* Exit program. */    
    exit( code );    
}    
  
void checkSDLError(int line = -1)  
{  
#ifndef NDEBUG  
        const char *error = SDL_GetError();  
        if (*error != '\0')  
        {  
                printf("SDL Error: %s\n", error);  
                if (line != -1)  
                        printf(" + line: %i\n", line);  
                SDL_ClearError();  
        }  
#endif  
}  
  
void initGL( int width, int height )    
{    
    float ratio = (float) width / (float) height;    
    // Our shading model--Gouraud (smooth).    
    glShadeModel( GL_SMOOTH );    
    // Set the clear color.    
    glClearColor( 0, 0, 0, 0 );    
    // Setup our viewport.    
    glViewport( 0, 0, width, height );    
    //Change to the projection matrix and set our viewing volume.    
    glMatrixMode( GL_PROJECTION );    
    glLoadIdentity();    
    gluPerspective( 60.0, ratio, 1.0, 100.0 );    
}    
  
void initSDL()  
{  
    if (SDL_Init(SDL_INIT_VIDEO) < 0) /* Initialize SDL's Video subsystem */  
        sdldie("Unable to initialize SDL"); /* Or die on error */  
   
    /* Request opengl 3.2 context. 
     * SDL doesn't have the ability to choose which profile at this time of writing, 
     * but it should default to the core profile */  
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);  
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);  
   
    /* Turn on double buffering with a 24bit Z buffer. 
     * You may need to change this to 16 or 32 for your system */  
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);  
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);  
   
    /* Create our window centered at 512x512 resolution */  
    mainwindow = SDL_CreateWindow("OpenGL in SDL2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,  
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);  
    if (!mainwindow) /* Die if creation failed */  
        sdldie("Unable to create window");  
   
    checkSDLError(__LINE__);  
   
    /* Create our opengl context and attach it to our window */  
    maincontext = SDL_GL_CreateContext(mainwindow);  
    checkSDLError(__LINE__);  
   
    /* This makes our buffer swap syncronized with the monitor's vertical refresh */  
    SDL_GL_SetSwapInterval(1);  
}  
  
void renderGL()  
{  
     // Clear the color and depth buffers.    
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );    
    // We don't want to modify the projection matrix. */    
    glMatrixMode( GL_MODELVIEW );    
    glLoadIdentity( );    
    // Move down the z-axis.    
    glTranslatef( 0.0, 0.0, -5.0 );    
    //Draw a square    
    glBegin(GL_QUADS);    
    glColor3f(1.0f,0.0f,0.0f);    
    glVertex3f(-1.0f  , -1.0f  ,  1.0f  );    
    glColor3f(0.0f,1.0f,0.0f);    
    glVertex3f( 1.0f  , -1.0f  ,  1.0f  );    
    glColor3f(0.0f,0.0f,1.0f);    
    glVertex3f( 1.0f  ,  1.0f  ,  1.0f  );    
    glColor3f(1.0f,1.0f,0.0f);    
    glVertex3f(-1.0f  ,  1.0f  ,  1.0f  );    
    glEnd();    
    SDL_GL_SwapWindow(mainwindow);  
}  
  
void resizeGL(int width,int height)    
{    
    if ( height == 0 )    
    {    
        height = 1;    
    }    
    //Reset View    
    glViewport( 0, 0, (GLint)width, (GLint)height );    
    //Choose the Matrix mode    
    glMatrixMode( GL_PROJECTION );    
    //reset projection    
    glLoadIdentity();    
    //set perspection    
    gluPerspective( 45.0, (GLfloat)width/(GLfloat)height, 0.1, 100.0 );    
    //choose Matrix mode    
    glMatrixMode( GL_MODELVIEW );    
    glLoadIdentity();    
      
}    
  
void handleKeyEvent( SDL_Keysym* keysym )    
{    
    switch( keysym->sym )    
    {    
    case SDLK_ESCAPE:    
        quit( 0 );    
        break;    
    case SDLK_SPACE:    
     cout<<"Space"<<endl;  
        break;    
    default:    
        break;    
    }    
}   
  
void handleEvents()    
{    
    // Our SDL event placeholder.    
    SDL_Event event;    
    //Grab all the events off the queue.    
    while( SDL_PollEvent( &event ) ) {    
        switch( event.type ) {    
        case SDL_KEYDOWN:    
            // Handle key Event    
            handleKeyEvent( &event.key.keysym );    
            break;    
        case SDL_QUIT:    
            // Handle quit requests (like Ctrl-c).    
            quit( 0 );    
            break;    
        case SDL_WINDOWEVENT:    
            if(event.window.event == SDL_WINDOWEVENT_RESIZED)  
            {  
                if ( mainwindow )    
                {    
                    int tmpX,tmpY;  
                    SDL_GetWindowSize(mainwindow,&tmpX,&tmpY);  
                    resizeGL(tmpX, tmpY);   
                      
                }    
            }  
            SDL_GL_SwapWindow(mainwindow);  
            break;    
        }    
    }    
}   
  
/* Our program's entry point */  
int main(int argc, char *argv[])  
{  
    initSDL();  
    initGL(SCREEN_WIDTH, SCREEN_HEIGHT);  
    renderGL( );    
    while(true)  
    {  
        /* Process incoming events. */    
        handleEvents( );    
        /*Render scene*/  
        renderGL();   
    }  
   
    return 0;  
}  

