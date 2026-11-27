// The game's own loading screen, told to the plugin (TandemNative.LoadingSign):
// the watchdog and the observer take it as a pause, as the pause menu, while
// the script tick waits behind it and the engine's frames may stop for
// seconds (the world streaming: a fast travel's frames read as a slow frame,
// a loading screen of 30 s as a stall; tandem/observer.hpp LoadingShown,
// docs/architecture/observer.md).
//
// The three loading screens' controllers (native classes with script
// bodies, final.reds (2.50):336614, 336643, 337605): the save's
// (InitialLoadingScreenLogicController), the fallback
// (DefaultLoadingScreenLogicController) and the fast travel's
// (LoadingScreenLogicController, inkFastTravelLoadingScreenLogicController).
// A sign when one starts, at each progress the engine gives it, and every
// 0.25 s while its widget lives and shows (an animation that changes
// nothing: a text value on a compound widget, as the LoadBegone mod ticks its
// own). Each only calls the native, an atomic store: safe on any thread.

public abstract class TandemLoading {
  // A ticker on the loading screen's root: a sign each loop.
  public static func Tick(root: ref<inkWidget>, owner: ref<IScriptable>) -> ref<inkAnimProxy> {
    TandemNative.LoadingSign();
    if !IsDefined(root) {
      return null;
    }
    let wait = new inkAnimTextValueProgress();
    wait.SetStartProgress(0.0);
    wait.SetEndProgress(0.0);
    wait.SetDuration(0.25);
    let def = new inkAnimDef();
    def.AddInterpolator(wait);
    let options: inkAnimOptions;
    options.loopInfinite = true;
    options.loopType = inkanimLoopType.Cycle;
    let proxy = root.PlayAnimationWithOptions(def, options);
    if IsDefined(proxy) {
      proxy.RegisterToCallback(inkanimEventType.OnStartLoop, owner, n"OnTandemLoadingTick");
    }
    return proxy;
  }

  public static func Seen(root: ref<inkWidget>) -> Void {
    if IsDefined(root) && root.IsVisible() {
      TandemNative.LoadingSign();
    }
  }
}

// ---- the save's loading screen ----

@addField(InitialLoadingScreenLogicController)
private let m_tandemLoadingTicker: ref<inkAnimProxy>;

// job thread safe: it calls a native that only stores an atomic, and keeps
// its ticker in a field of the wrapped controller.
@wrapMethod(InitialLoadingScreenLogicController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  this.m_tandemLoadingTicker = TandemLoading.Tick(this.GetRootWidget(), this);
  return result;
}

@addMethod(InitialLoadingScreenLogicController)
protected cb func OnTandemLoadingTick(proxy: ref<inkAnimProxy>) -> Bool {
  TandemLoading.Seen(this.GetRootWidget());
  return true;
}

@wrapMethod(InitialLoadingScreenLogicController)
protected final func SetLoadProgress(progress: Float) -> Void {
  TandemNative.LoadingSign();
  wrappedMethod(progress);
}

// ---- the fallback loading screen ----

@addField(DefaultLoadingScreenLogicController)
private let m_tandemLoadingTicker: ref<inkAnimProxy>;

// job thread safe: it calls a native that only stores an atomic, and keeps
// its ticker in a field of the wrapped controller.
@wrapMethod(DefaultLoadingScreenLogicController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  this.m_tandemLoadingTicker = TandemLoading.Tick(this.GetRootWidget(), this);
  return result;
}

@addMethod(DefaultLoadingScreenLogicController)
protected cb func OnTandemLoadingTick(proxy: ref<inkAnimProxy>) -> Bool {
  TandemLoading.Seen(this.GetRootWidget());
  return true;
}

@wrapMethod(DefaultLoadingScreenLogicController)
protected final func SetLoadProgress(progress: Float) -> Void {
  TandemNative.LoadingSign();
  wrappedMethod(progress);
}

// ---- the fast travel's loading screen ----

@addField(LoadingScreenLogicController)
private let m_tandemLoadingTicker: ref<inkAnimProxy>;

// job thread safe: it calls a native that only stores an atomic, and keeps
// its ticker in a field of the wrapped controller.
@wrapMethod(LoadingScreenLogicController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  this.m_tandemLoadingTicker = TandemLoading.Tick(this.GetRootWidget(), this);
  return result;
}

@addMethod(LoadingScreenLogicController)
protected cb func OnTandemLoadingTick(proxy: ref<inkAnimProxy>) -> Bool {
  TandemLoading.Seen(this.GetRootWidget());
  return true;
}

@wrapMethod(LoadingScreenLogicController)
protected final func SetLoadProgress(progress: Float) -> Void {
  TandemNative.LoadingSign();
  wrappedMethod(progress);
}
