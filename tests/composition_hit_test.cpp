#include <windows.h>
#include <cstdio>
#include <vector>
#include "../native/composition_surface.h"
int leftClicks=0,rightClicks=0;
LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM w,LPARAM l){
 if(msg==WM_LBUTTONUP){++leftClicks;return 0;}if(msg==WM_RBUTTONUP){++rightClicks;return 0;}
 if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(msg==WM_ERASEBKGND)return 1;
 return DefWindowProcW(h,msg,w,l);
}
int main(){
 WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"CompositionHitFixture";RegisterClassW(&cls);
 HWND h=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"Composition hit fixture",WS_POPUP,200,200,180,24,nullptr,nullptr,cls.hInstance,nullptr);
 CompositionSurface renderer;std::vector<unsigned> transparent(180*24,0);
 const bool initialized=h && renderer.Initialize(h);
 const bool painted=initialized && renderer.Paint(transparent.data(),180,24,180*4);
 const POINT points[]={{201,201},{378,201},{201,222},{378,222},{290,212}};
 HWND hits[5]{};
 const bool positioned=SetWindowPos(h,HWND_TOPMOST,200,200,180,24,SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE;
 bool ok=painted;
 // Collect evidence without logging, waiting or retrying before hit checks.
 // A failure must distinguish GPU initialization from an unexpected covering window.
 for(unsigned i=0;i<5;++i){hits[i]=WindowFromPoint(points[i]);ok=(hits[i]==h) && ok;}
 for(int i=0;i<20;++i){SendMessageW(h,WM_LBUTTONUP,0,0);SendMessageW(h,WM_RBUTTONUP,0,0);}
 ok=ok && leftClicks==20 && rightClicks==20;
 if(!ok){
     RECT rect{};GetWindowRect(h,&rect);
     std::printf("create=%d initialize=%d paint=%d show=%d dpi=%u rect=%ld,%ld,%ld,%ld left=%d right=%d\n",
                 h!=nullptr,initialized,painted,positioned,GetDpiForWindow(h),rect.left,rect.top,rect.right,rect.bottom,leftClicks,rightClicks);
     for(unsigned i=0;i<5;++i) if(hits[i]!=h){
         wchar_t cls[128]{};GetClassNameW(hits[i],cls,128);
         std::printf("hit(%ld,%ld)=%p expected=%p class=%ls\n",points[i].x,points[i].y,hits[i],h,cls);
     }
 }
 renderer.Reset();DestroyWindow(h);
 std::printf("%s: transparent corners/center hit checks; left=%d/20 right=%d/20\n",ok?"PASS":"FAIL",leftClicks,rightClicks);return ok?0:1;
}
