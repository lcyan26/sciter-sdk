#include "sciter_android_bridge.h"

// include packaged sdk/demos.lite/facade/ folder
#include "../../../demos.lite/facade-resources.cpp"


UINT engine_handle_sciter_notification(LPSCITER_CALLBACK_NOTIFICATION pnm,LPVOID callbackParam);


// initialize and create Sciter instance associated with engine* pointer 
bool engine_init(struct engine *engine) {
  // enable features to be used from script
  BOOL r = SciterSetOption(NULL, SCITER_SET_SCRIPT_RUNTIME_FEATURES, ALLOW_FILE_IO | ALLOW_SOCKET_IO | ALLOW_EVAL | ALLOW_SYSINFO);
  if (!r) return false;

  char lang[4] = {0};
  char country[4] = {0};
  AConfiguration_getLanguage(engine->app->config, lang);
  AConfiguration_getCountry(engine->app->config, country);
  sciter::value map;
  map.set_item("lang", sciter::value(aux::utf2w(lang,2).chars()));
  map.set_item("country", sciter::value(aux::utf2w(country, 2).chars()));
  SciterSetMediaVars(NULL, &map);

  // setup archive with resources in ../../../demos.lite/facade-resources.cpp
  sciter::archive::instance().open(aux::elements_of(resources)); // bind resources[] (defined in "facade-resources.cpp") with the archive

  // create Sciter instance and associate it with engine
  r = SciterProcX(engine, SCITER_X_MSG_CREATE());
  if (!r) return false;
  
  SciterSetCallback(engine, engine_handle_sciter_notification, NULL);

  unsigned int dpi = (unsigned)AConfiguration_getDensity(engine->app->config);
  if (dpi < 96) dpi = 96; else if (dpi > ACONFIGURATION_DENSITY_XXXHIGH) dpi = ACONFIGURATION_DENSITY_XXXHIGH;

  SciterProcX(engine, SCITER_X_MSG_RESOLUTION(dpi));

  // load doc either from assets
  //r = SciterLoadFile(engine, WSTR("asset:main.htm"));
  // or from the archive
  r = SciterLoadFile(engine, WSTR("this://app/main.htm"));
  
  return bool(r);
}

bool engine_shutdown(struct engine *engine) {

  SciterProcX(engine, SCITER_X_MSG_DESTROY());
  return true;
}

int engine_init_display(struct engine *engine) {
  // initialize OpenGL ES and EGL

  /*
   * Here specify the attributes of the desired configuration.
   * Below, we select an EGLConfig with at least 8 bits per color
   * component compatible with on-screen windows
   */
  const EGLint attribs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                            EGL_BLUE_SIZE,    8,
                            EGL_GREEN_SIZE,   8,
                            EGL_RED_SIZE,     8,
                            EGL_DEPTH_SIZE,   0,
                            EGL_STENCIL_SIZE, 8,
                            EGL_NONE};

  EGLint       w, h, format;
  EGLint       numConfigs;
  EGLConfig    config;
  EGLSurface   surface;
  EGLContext   context;

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

  eglInitialize(display, 0, 0);

  /* Here, the application chooses the configuration it desires. In this
   * sample, we have a very simplified selection process, where we pick
   * the first EGLConfig that matches our criteria */
  eglChooseConfig(display, attribs, &config, 1, &numConfigs);

  /* EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
   * guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
   * As soon as we picked a EGLConfig, we can safely reconfigure the
   * ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID. */
  eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

  ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, format);

  surface = eglCreateWindowSurface(display, config, engine->app->window, NULL);

  
  EGLint openGLVersionRequested = 3;
  EGLint contextAttribs[]       = {
      EGL_CONTEXT_CLIENT_VERSION,
      openGLVersionRequested, // selects OpenGL ES 3.0, set to 2 to select
                              // OpenGL ES 2.0
      EGL_NONE};

  context = eglCreateContext(display, config, NULL, contextAttribs);
  //context = eglCreateContext(display, config, NULL, NULL);

  if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
    LOGW("Unable to eglMakeCurrent");
    return -1;
  }

  eglQuerySurface(display, surface, EGL_WIDTH, &w);
  eglQuerySurface(display, surface, EGL_HEIGHT, &h);

  engine->display     = display;
  engine->context     = context;
  engine->surface     = surface;
  engine->width       = w;
  engine->height      = h;
  engine->state.angle = 0;

  // Initialize GL state.
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
  glEnable(GL_CULL_FACE);
  glShadeModel(GL_SMOOTH);
  glDisable(GL_DEPTH_TEST);

  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 0);
  glClearStencil(0);
  glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  
  SciterProcX(engine, SCITER_X_MSG_SIZE(w, h));

  return 0;
}

void engine_draw_frame(struct engine *engine) {
  if (engine->display == NULL) {
    // No display.
    return;
  }

  // Just fill the screen with a color.
  //glClearColor(((float)engine->state.x) / engine->width, engine->state.angle,
  //             ((float)engine->state.y) / engine->height, 1);
  //glClear(GL_COLOR_BUFFER_BIT);
  SciterProcX(engine, SCITER_X_MSG_HEARTBIT(0));

  SciterProcX(engine, SCITER_X_MSG_PAINT());
  
  eglSwapBuffers(engine->display, engine->surface);
}

/**
 * Tear down the EGL context currently associated with the display.
 */
void engine_term_display(struct engine *engine) {
  if (engine->display != EGL_NO_DISPLAY) {
    eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (engine->context != EGL_NO_CONTEXT) {
      eglDestroyContext(engine->display, engine->context);
    }
    if (engine->surface != EGL_NO_SURFACE) {
      eglDestroySurface(engine->display, engine->surface);
    }
    eglTerminate(engine->display);
  }
  engine->animating = 0;
  engine->display   = EGL_NO_DISPLAY;
  engine->context   = EGL_NO_CONTEXT;
  engine->surface   = EGL_NO_SURFACE;
}

/**
 * Process the next input event.
 */
int32_t engine_handle_input(struct android_app *app, AInputEvent *event) {
  struct engine *engine = (struct engine *)app->userData;
  if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {

    POINT pos;
    int   action        = AMotionEvent_getAction(event);
    int pointer_index  = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                        AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    pos.x = AMotionEvent_getX(event, pointer_index);
    pos.y = AMotionEvent_getY(event, pointer_index);
    if (action == AMOTION_EVENT_ACTION_MOVE)
      return SciterProcX(engine,SCITER_X_MSG_MOUSE(MOUSE_MOVE, MAIN_MOUSE_BUTTON, KEYBOARD_STATES(0), pos));
    else if (action == AMOTION_EVENT_ACTION_DOWN)
      return SciterProcX(engine,SCITER_X_MSG_MOUSE(MOUSE_DOWN, MAIN_MOUSE_BUTTON, KEYBOARD_STATES(0), pos));
    else if (action == AMOTION_EVENT_ACTION_UP)
      return SciterProcX(engine, SCITER_X_MSG_MOUSE(MOUSE_UP, MAIN_MOUSE_BUTTON, KEYBOARD_STATES(0), pos));
    return 0;
  }
  return 0;
}

/**
 * Process the next main command.
 */
void engine_handle_cmd(struct android_app *app, int32_t cmd) {
  struct engine *engine = (struct engine *)app->userData;
  switch (cmd) {

    case APP_CMD_INIT_WINDOW:
      // The window is being shown, get it ready.
      if (engine->app->window != NULL) {
        engine_init(engine);
        engine_init_display(engine);
        engine_draw_frame(engine);
      }
      break;

    case APP_CMD_SAVE_STATE:
      // The system has asked us to save our current state.  Do so.
      engine->app->savedState = malloc(sizeof(struct saved_state));
      *((struct saved_state *)engine->app->savedState) = engine->state;
      engine->app->savedStateSize = sizeof(struct saved_state);
      break;
    case APP_CMD_TERM_WINDOW:
      // The window is being hidden or closed, clean it up.
      engine_shutdown(engine);
      engine_term_display(engine);
      break;
    case APP_CMD_GAINED_FOCUS:
      // When our app gains focus, we start monitoring the accelerometer.
      if (engine->accelerometerSensor != NULL) {
        ASensorEventQueue_enableSensor(engine->sensorEventQueue,
                                       engine->accelerometerSensor);
        // We'd like to get 60 events per second (in us).
        ASensorEventQueue_setEventRate(engine->sensorEventQueue,
                                       engine->accelerometerSensor,
                                       (1000L / 60) * 1000);
      }
      break;
    case APP_CMD_LOST_FOCUS:
      // When our app loses focus, we stop monitoring the accelerometer.
      // This is to avoid consuming battery while not being used.
      if (engine->accelerometerSensor != NULL) {
        ASensorEventQueue_disableSensor(engine->sensorEventQueue,
                                        engine->accelerometerSensor);
      }
      // Also stop animating.
      engine->animating = 0;
      engine_draw_frame(engine);
      break;

    case APP_CMD_CONTENT_RECT_CHANGED:
      {
        unsigned w = ANativeWindow_getWidth(app->window);
        unsigned h = ANativeWindow_getHeight(app->window);
        sciter::value vars;
        vars.set_item("window-content-top",sciter::value(app->contentRect.top));
        vars.set_item("window-content-left",sciter::value(app->contentRect.left));
        vars.set_item("window-content-bottom",sciter::value(app->contentRect.bottom));
        vars.set_item("window-content-right",sciter::value(app->contentRect.right));
        SciterSetMediaVars(engine, &vars);
        SciterProcX(engine, SCITER_X_MSG_SIZE(w,h));
      } break;

  }
}

UINT engine_on_load_data(LPSCN_LOAD_DATA pnm) {
  
  struct engine *engine = (struct engine *)pnm->hwnd;

  LPCBYTE pb = 0; UINT cb = 0;
  aux::wchars wu = aux::chars_of(pnm->uri);

  if (wu.like(WSTR("this://app/*"))) { // handle this://app/ URL schema
    // try to get them from archive first
    aux::bytes adata = sciter::archive::instance().get(wu.start + 11);
    if (adata.length) 
      ::SciterDataReady(pnm->hwnd, pnm->uri, adata.start, adata.length);
    return LOAD_OK;
  }
   
  AAssetManager *mgr    = engine->app->activity->assetManager;
  if (!mgr) return LOAD_OK;
  // only handle requests for "asset:something.anything" here:
  aux::wchars url = aux::chars_of(pnm->uri);
  if (url.index_of(const_wchars("asset:")) != 0) // handle asset:... resources
    return LOAD_OK; // not ours
  url.prune(6); // skip "asset:"
  aux::w2utf asset_name(url);
  AAsset *   asset = AAssetManager_open(mgr, asset_name, AASSET_MODE_BUFFER);
  if (!asset) return LOAD_DISCARD;
  SciterDataReady(engine, pnm->uri,(LPCBYTE)AAsset_getBuffer(asset),AAsset_getLength(asset));
  AAsset_close(asset);
  return LOAD_OK;
}

UINT engine_on_data_loaded(LPSCN_DATA_LOADED pnm) { return 0; }

UINT engine_attach_behavior(LPSCN_ATTACH_BEHAVIOR lpab) {
  // attach native behaviors (if we have any)
  sciter::event_handler *pb = sciter::behavior_factory::create(lpab->behaviorName, lpab->element);
  if (pb) {
    lpab->elementTag  = pb;
    lpab->elementProc = sciter::event_handler::element_proc;
    return 1;
  }
  return 0;
}

// notifiaction cracker
UINT engine_handle_sciter_notification(LPSCITER_CALLBACK_NOTIFICATION pnm, LPVOID callbackParam)
{
  // Crack and call appropriate method
  // here are all notifiactions
  switch (pnm->code) {
    case SC_LOAD_DATA: return engine_on_load_data((LPSCN_LOAD_DATA)pnm);
    case SC_DATA_LOADED: return engine_on_data_loaded((LPSCN_DATA_LOADED)pnm);
    case SC_ATTACH_BEHAVIOR: return engine_attach_behavior((LPSCN_ATTACH_BEHAVIOR)pnm);
    case SC_ENGINE_DESTROYED: break;
  }
  return 0;
}








