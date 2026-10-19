-- Ordinary application entrypoint: behavior and pages can be replaced at runtime.
local config=notes.require('suite/config.lua')
local app=notes.require('suite/main.lua')
function on_prepare() app.prepare(config) end
function on_start() app.start(config) end
