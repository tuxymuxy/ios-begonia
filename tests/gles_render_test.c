/* Authored hardware GLES clear/triangle test. Software fallback is a failure. */
#define GL_GLES_PROTOTYPES 0
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "native_gpu_diagnostics.h"
#define FUNCTIONS(X) X(GETSTRING,glGetString) X(GETERROR,glGetError) X(VIEWPORT,glViewport) X(CLEARCOLOR,glClearColor) X(CLEAR,glClear) X(FINISH,glFinish) X(CREATESHADER,glCreateShader) X(SHADERSOURCE,glShaderSource) X(COMPILESHADER,glCompileShader) X(GETSHADERIV,glGetShaderiv) X(GETSHADERINFOLOG,glGetShaderInfoLog) X(CREATEPROGRAM,glCreateProgram) X(ATTACHSHADER,glAttachShader) X(LINKPROGRAM,glLinkProgram) X(GETPROGRAMIV,glGetProgramiv) X(USEPROGRAM,glUseProgram) X(DRAWARRAYS,glDrawArrays) X(READPIXELS,glReadPixels) X(DELETEPROGRAM,glDeleteProgram) X(DELETESHADER,glDeleteShader)
#define DECLARE(type,name) PFNGL##type##PROC name;
FUNCTIONS(DECLARE)
#undef DECLARE
static GLuint shader(GLenum type,const char *source) {
 GLuint object=glCreateShader(type);glShaderSource(object,1,&source,NULL);glCompileShader(object);GLint good=0;glGetShaderiv(object,GL_COMPILE_STATUS,&good);
 if(!good){char log[1024]={0};glGetShaderInfoLog(object,sizeof log,NULL,log);printf("BEGONIA GLES: shader failure %s\n",log);return 0;}return object;
}
int main(void) {
 setvbuf(stdout,NULL,_IOLBF,0);native_gpu_install_crash_diagnostics();alarm(60);
 puts("BEGONIA GLES: library begin");void *library=dlopen("/redwood/lib/libOSMesa.8.dylib",RTLD_NOW|RTLD_LOCAL);
 if(!library){printf("BEGONIA GLES: dlopen failed %s\n",dlerror());return 1;}
 typedef void *Context;Context(*create)(const int *,Context)=dlsym(library,"OSMesaCreateContextAttribs");
 unsigned char(*make_current)(Context,void *,unsigned,int,int)=dlsym(library,"OSMesaMakeCurrent");
 void(*destroy)(Context)=dlsym(library,"OSMesaDestroyContext");void(*(*get)(const char *))(void)=dlsym(library,"OSMesaGetProcAddress");
 if(!create||!make_current||!destroy||!get)return 1;native_gpu_record_icd((void *)create);
 const int attributes[]={0x22,GL_RGBA,0x30,24,0x31,8,0x33,0x38,0x36,3,0x37,0,0};
 puts("BEGONIA GLES: context begin");Context context=create(attributes,NULL);if(!context){puts("BEGONIA GLES: context absent");return 1;}
 unsigned char *pixels=calloc(64*64,4);if(!pixels || !make_current(context,pixels,GL_UNSIGNED_BYTE,64,64))return 1;
#define LOAD(type,name) name=(PFNGL##type##PROC)get(#name);if(!name){printf("BEGONIA GLES: missing %s\n",#name);return 1;}
 FUNCTIONS(LOAD)
#undef LOAD
 const char *renderer=(const char *)glGetString(GL_RENDERER);printf("BEGONIA GLES: renderer=%s\n",renderer?renderer:"none");
 if(!renderer || !strstr(renderer,"Mali-G76") || !strstr(renderer,"Panfrost")) {puts("BEGONIA GLES: refused unverified/software renderer");return 1;}
 glViewport(0,0,64,64);glClearColor(0.25f,0.5f,0.75f,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();unsigned char clear[4]={0};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,clear);
 int clear_good=clear[0]>=63&&clear[0]<=64&&clear[1]>=127&&clear[1]<=128&&clear[2]>=191&&clear[2]<=192&&clear[3]==255;
 printf("BEGONIA GLES: hardware clear %s rgba=%u/%u/%u/%u\n",clear_good?"PASS":"FAIL",clear[0],clear[1],clear[2],clear[3]);
 alarm(20);const char *vertex="#version 300 es\nvoid main(){vec2 p[3]=vec2[3](vec2(-1.,-1.),vec2(3.,-1.),vec2(-1.,3.));gl_Position=vec4(p[gl_VertexID],0.,1.);}";
 const char *fragment="#version 300 es\nprecision highp float;out vec4 color;void main(){color=vec4(0.,1.,0.,1.);}";
 GLuint vs=shader(GL_VERTEX_SHADER,vertex),fs=shader(GL_FRAGMENT_SHADER,fragment);if(!vs||!fs)return 1;GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 1;
 glUseProgram(program);glDrawArrays(GL_TRIANGLES,0,3);glFinish();unsigned char result[4]={0};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,result);GLenum error=glGetError();
 int triangle_good=result[0]==0&&result[1]==255&&result[2]==0&&result[3]==255&&error==GL_NO_ERROR;
 printf("BEGONIA GLES: authored GPU triangle %s rgba=%u/%u/%u/%u error=%x\n",triangle_good?"PASS":"FAIL",result[0],result[1],result[2],result[3],error);
 glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);make_current(NULL,NULL,0,0,0);destroy(context);free(pixels);alarm(0);return clear_good&&triangle_good?0:1;
}
