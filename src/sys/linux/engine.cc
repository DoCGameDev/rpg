// This file is part of the :(){ :|:& };:'s project
// Licensing information can be found in the LICENSE file
// (C) 2014 :(){ :|:& };:. All rights reserved.
#include "sys/common.h"

// -----------------------------------------------------------------------------
static const char *wndTypePool[] = { "windowed", NULL };
CVar Engine::wndType("wndType", CVAR_INT | CVAR_CONFIG, "windowed", wndTypePool, "Window type");

// -----------------------------------------------------------------------------
// Converts an XKeySym to a unified, internal keyboard code
// -----------------------------------------------------------------------------
KeyButton TranslateKey(const KeySym& sym)
{
  switch (sym)
  {
    case XK_Escape:    return KEY_ESC;
    case XK_Shift_L:   return KEY_SHIFT;
    case XK_Shift_R:   return KEY_SHIFT;
    case XK_Control_L: return KEY_CTRL;
    case XK_Control_R: return KEY_CTRL;
    case XK_space:     return KEY_SPACE;
    case XK_Return:    return KEY_ENTER;
    case XK_a:         return KEY_A;
    case XK_w:         return KEY_W;
    case XK_s:         return KEY_S;
    case XK_d:         return KEY_D;
    default:           return KEY_UNDEF;
  }
}

// -----------------------------------------------------------------------------
// Linux implementation of platform-specific things
// -----------------------------------------------------------------------------
class EngineImpl : public Engine
{
public:
                        EngineImpl();
  void                  Init();
  void                  Run();
  void                  Destroy();
  uint64_t              GetTime();

private:
  void                  InitWindow();
  void                  UpdateWindow();
  void                  DestroyWindow();
  void                  InitLua();
  void                  DestroyLua();

  Display              *dpy;
  Window                wnd;
  Atom                  wndClose;
  GLXContext            context;
  Colormap              colormap;
  XF86VidModeModeInfo  *desktop;
  XF86VidModeModeInfo **modes;
  int                   modeCount;
};

// Engine instance
static EngineImpl engineImpl;
Engine *engine = &engineImpl;

// -----------------------------------------------------------------------------
EngineImpl::EngineImpl()
  : dpy(NULL)
  , wnd(0)
  , wndClose(0)
  , context(NULL)
  , colormap(0)
{
}

// -----------------------------------------------------------------------------
void EngineImpl::Init()
{
  InitLua();
  InitWindow();
  world->Init("assets/scripts/test.lua");
  renderer->Init();
  cache->Init();
  threadMngr->Init();
  threadMngr->Spawn(world);
  threadMngr->Spawn(network);
}

// -----------------------------------------------------------------------------
void EngineImpl::Destroy()
{
  threadMngr->Destroy();
  renderer->Destroy();
  cache->Destroy();
  world->Destroy();
  DestroyWindow();
  DestroyLua();
}

// -----------------------------------------------------------------------------
void EngineImpl::InitWindow()
{
  // Open the X display
  Window root;
  if (!(dpy = XOpenDisplay(0)) || !(root = DefaultRootWindow(dpy)))
  {
    EXCEPT << "Cannot open display";
  }

  // Check GLX version
  int min, maj;
  if (!glXQueryVersion(dpy, &maj, &min) || (maj == 1 && min < 3) || maj < 1)
  {
    EXCEPT << "Invalid GLX version (" << maj << "." << min << " < 1.3)";
  }

  // Retrieve visual info
  static int ATTR[] =
  {
    GLX_RGBA,
    GLX_DEPTH_SIZE, 24,
    GLX_DOUBLEBUFFER,
    None
  };

  XVisualInfo *vi;
  if (!(vi = glXChooseVisual(dpy, 0, ATTR)))
  {
    EXCEPT << "Cannot choose visual";
  }

  // Create colormap
  if (!(colormap = XCreateColormap(dpy, root, vi->visual, AllocNone)))
  {
    XFree(vi);
    EXCEPT << "Cannot create colormap";
  }

  // Create the window
  XSetWindowAttributes swa;
  swa.border_pixel = 0;
  swa.colormap = colormap;
  swa.event_mask = ExposureMask |
                   KeyPressMask | KeyReleaseMask |
                   ButtonPressMask | ButtonReleaseMask |
                   StructureNotifyMask;
  if (!(wnd = XCreateWindow(dpy, root, 0, 0,
                            wndWidth.GetInt(), wndHeight.GetInt(),
                            0, vi->depth, InputOutput, vi->visual,
                            CWBorderPixel | CWColormap | CWEventMask, &swa)))
  {
    EXCEPT << "Cannot create X window";
  }

  // Set window title
  XStoreName(dpy, wnd, wndTitle.GetString().c_str());

  // Catch window close events
  wndClose = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(dpy, wnd, &wndClose, 1);

  // Map the window
  XEvent evt;
  XMapWindow(dpy, wnd);
  XSync(dpy, False);
  do
  {
    XNextEvent(dpy, &evt);
  } while (evt.type != MapNotify);

  // Retrieve available fullscreen modes
  if (!XF86VidModeGetAllModeLines(dpy, DefaultScreen(dpy), &modeCount, &modes))
  {
    XFree(vi);
    EXCEPT << "Cannot retrieve fullscreen modes";
  }

  // Create the OpenGL context
  context = glXCreateContext(dpy, vi, NULL, GL_TRUE);
  XFree(vi);
  if (!context)
  {
    EXCEPT << "Cannot create OpenGL context";
  }

  // Initialise GLEW
  glXMakeCurrent(dpy, wnd, context);
  if (glewInit() != GLEW_OK)
  {
    EXCEPT << "Cannot initialise GLEW";
  }
}

// -----------------------------------------------------------------------------
void EngineImpl::DestroyWindow()
{
  if (context)
  {
    glXMakeCurrent(dpy, 0, 0);
    glXDestroyContext(dpy, context);
    context = NULL;
  }

  if (wnd)
  {
    if (wndType.GetInt() == 1)
    {
      XF86VidModeSwitchToMode(dpy, DefaultScreen(dpy), modes[0]);
      XF86VidModeSetViewPort(dpy, DefaultScreen(dpy), 0, 0);
    }

    XDestroyWindow(dpy, wnd);
    wnd = 0;
  }

  if (colormap)
  {
    XFreeColormap(dpy, colormap);
    colormap = 0;
  }

  if (dpy)
  {
    XCloseDisplay(dpy);
    dpy = NULL;
  }
}

// -----------------------------------------------------------------------------
void EngineImpl::UpdateWindow()
{
  XWindowAttributes attr;
  XSetWindowAttributes swa;

  XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &attr);
  XStoreName(dpy, wnd, wndTitle.GetString().c_str());

  switch (wndType.GetInt())
  {
    // Windowed mode
    case 0:
    {
      XF86VidModeSwitchToMode(dpy, DefaultScreen(dpy), modes[0]);
      XF86VidModeSetViewPort(dpy, DefaultScreen(dpy), 0, 0);

      attr.x = attr.x + ((attr.width - wndWidth.GetInt()) >> 1);
      attr.y = attr.y + ((attr.height - wndHeight.GetInt()) >> 1);
      attr.width = wndWidth.GetInt();
      attr.height = wndHeight.GetInt();

      XMoveResizeWindow(dpy, wnd, attr.x, attr.y, attr.width, attr.height);
      Renderer::vpWidth.SetInt(attr.width);
      Renderer::vpHeight.SetInt(attr.height);
      Renderer::vpReload.SetBool(true);

      swa.override_redirect = False;
      XChangeWindowAttributes(dpy, wnd, CWOverrideRedirect, &swa);
      break;
    }
  }
}

// -----------------------------------------------------------------------------
void EngineImpl::Run()
{
  XEvent xevt;
  XWindowAttributes attr;
  InputEvent event;

  int a = 0;
  running = true;

  threadMngr->Start();
  while (running)
  {
    while (XPending(dpy) > 0)
    {
      XNextEvent(dpy, &xevt);
      switch (xevt.type)
      {
        case ConfigureNotify:
        case ResizeRequest:
        {
          break;
        }
        case ClientMessage:
        {
          if (xevt.xclient.data.l[0] == (int)wndClose)
          {
            engine->Quit();
            break;
          }
          break;
        }
        case KeyPress:
        {
          event.type = EVT_KEYBOARD;
          event.keyboard.state = true;
          event.keyboard.key = TranslateKey(XLookupKeysym(&xevt.xkey, 0));
          world->PostEvent(event);
          break;
        }
        case KeyRelease:
        {
          event.type = EVT_KEYBOARD;
          event.keyboard.state = false;
          event.keyboard.key = TranslateKey(XLookupKeysym(&xevt.xkey, 0));
          world->PostEvent(event);
          break;
        }
      }
    }

    if (engine->IsRunning())
    {
      if (wndReload.GetBool())
      {
        UpdateWindow();
        wndReload.SetBool(false);
      }

      renderer->Frame();
      glXSwapBuffers(dpy, wnd);
    }
  }

  threadMngr->Stop();
}

// -----------------------------------------------------------------------------
uint64_t EngineImpl::GetTime()
{
  struct timespec tv;
  clock_gettime(CLOCK_REALTIME, &tv);
  return (uint64_t)tv.tv_sec * 1000000000ull + (uint64_t)tv.tv_nsec;
}

// -----------------------------------------------------------------------------
void EngineImpl::InitLua()
{

}

// -----------------------------------------------------------------------------
void EngineImpl::DestroyLua()
{

}

// -----------------------------------------------------------------------------
int main(int argc, char **argv)
{
  try
  {
    engine->Init();
    engine->Run();
    engine->Destroy();
    return EXIT_SUCCESS;
  }
  catch (std::exception& e)
  {
    std::cerr << "[Main]" << e.what() << std::endl;
    engine->Destroy();
    return EXIT_FAILURE;
  }
}
