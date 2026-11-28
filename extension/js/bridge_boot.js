// Host bootstrap for the generic UNO bridge (PLAN_FULL_API.md section 2, F3). Evaluated once per engine after
// bridge.js. The result is a function that takes the native primitive and returns a factory. The host calls the
// factory once per command with the policy object and gets the bridge for that command.
// createBridge is removed from the global object, so plugin code cannot build a bridge around a function of its
// own. The host still enforces its own policy and allowlist in C++ (src/bridge_native.cxx).
(function (native) {
  var make = globalThis.createBridge;
  delete globalThis.createBridge;
  return function (policy) {
    return make(native, policy, 1);
  };
})
