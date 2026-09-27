-- ============================================================
--  mod-voicechat - test of the Lua bridge from voice/NativeUi.cpp (Lua 5.1 like WoW 3.3.5)
--  SPDX-License-Identifier: GPL-2.0-or-later
--
--  DE: Baut die benoetigten WoW-Funktionen nach und prueft Overrides, Event-Zustellung
--      und das einmalige Eintragen des Voice-Optionsmenues.
--  EN: Mocks the needed WoW functions and checks overrides, event delivery and adding
--      the voice options menu exactly once.
--  Usage: lua5.1 nativeui_test.lua <extracted-bridge.lua>
-- ============================================================
local bridge = assert(io.open(arg[1])):read("*a")
local fails = 0
local function check(cond, msg) if not cond then fails = fails + 1; print("FAIL " .. msg) end end

-- --- WoW-Nachbau / WoW mocks ---
local units = { player = "Me", party1 = "Bob", party2 = "Carl" }
function UnitName(u) return units[u] end
function UnitIsTalking(name) return nil end
function GetVoiceStatus(unit, mode) return nil end
local allowed = true
function IsVoiceChatAllowedByServer() return allowed end
local seen = {}
local function frame(events, name)
  local f = { name = name, events = events }
  function f:IsEventRegistered(e) return self.events[e] == true end
  function f:GetScript(kind)
    if kind ~= "OnEvent" then return nil end
    return function(self, ev, a1)
      table.insert(seen, self.name .. ":" .. ev .. ":" .. tostring(a1) .. ":" .. tostring(arg1))
      if self.name == "bad" then error("handler error must not break delivery") end
    end
  end
  return f
end
local frames = {
  frame({ VOICE_START = true }, "bad"),   -- DE: zuerst, Fehler darf die anderen nicht stoppen / first, its error must not stop the others
  frame({ VOICE_START = true, VOICE_STOP = true }, "player"),
  frame({ VOICE_START = true, VOICE_STOP = true, VOICE_STATUS_UPDATE = true }, "party"),
  frame({}, "other"),
}
function EnumerateFrames(f)
  if not f then return frames[1] end
  for i, x in ipairs(frames) do if x == f then return frames[i + 1] end end
end
AudioOptionsFrame = { categoryList = {} }
local panelRegistered = true
AudioOptionsVoicePanel = { IsEventRegistered = function(self, e) return panelRegistered and e == "PLAYER_ENTERING_WORLD" end }
local panelInit = 0
function OptionsFrame_AddCategory(frameObj, panel) table.insert(frameObj.categoryList, panel) end
function BlizzardOptionsPanel_OnEvent(panel, event) if event == "PLAYER_ENTERING_WORLD" then panelInit = panelInit + 1 end end

local function run(extra)
  local chunk, err = loadstring(bridge .. "\n" .. (extra or ""))
  check(chunk ~= nil, "bridge compiles: " .. tostring(err))
  local ok, perr = pcall(chunk)
  check(ok, "bridge runs: " .. tostring(perr))
end

-- 1) Menue noch vom Blizzard-Code registriert -> nicht eintragen / still registered by Blizzard -> do not add
run()
check(#AudioOptionsFrame.categoryList == 0, "no category while PLAYER_ENTERING_WORLD is pending")

-- 2) Danach genau einmal eintragen, auch bei mehrfacher Installation / afterwards add exactly once
panelRegistered = false
run(); run(); run()
check(#AudioOptionsFrame.categoryList == 1, "category added exactly once")
check(panelInit == 1, "panel initialised once")

-- 3) Sprecher + Voice-Status / speakers + voice status
seen = {}
run('MVC.Sync({["Me"]=1,["Bob"]=1},{["Me"]=1,["Bob"]=1})')
local joined = table.concat(seen, " ")
check(joined:find("party:VOICE_STATUS_UPDATE:nil") ~= nil, "VOICE_STATUS_UPDATE delivered")
check(joined:find("player:VOICE_START:player:player") ~= nil, "VOICE_START player (arg + global arg1)")
check(joined:find("party:VOICE_START:party1:party1") ~= nil, "VOICE_START party1")
check(joined:find("other:") == nil, "unregistered frames get nothing")
check(joined:find("bad:VOICE_START:player") ~= nil and joined:find("party:VOICE_START:player") ~= nil, "delivery continues after a failing handler")
check(UnitIsTalking("Bob") == 1 and UnitIsTalking("Carl") == nil, "UnitIsTalking override")
check(GetVoiceStatus("party1") == 1 and GetVoiceStatus("party2") == nil, "GetVoiceStatus override")
check(arg1 == nil, "global arg1 restored")

-- 4) Keine Aenderung -> keine Events / no change -> no events
seen = {}
run('MVC.Sync({["Me"]=1,["Bob"]=1},{["Me"]=1,["Bob"]=1})')
check(#seen == 0, "no events without change")

-- 5) Bob hoert auf, Carl ohne Voice / Bob stops, Carl without voice
seen = {}
run('MVC.Sync({["Me"]=1},{["Me"]=1})')
joined = table.concat(seen, " ")
check(joined:find("party:VOICE_STOP:party1") ~= nil, "VOICE_STOP party1")
check(joined:find("VOICE_STOP:player") == nil, "player keeps talking")
check(joined:find("party:VOICE_STATUS_UPDATE") ~= nil, "status update on voice set change")
check(UnitIsTalking("Bob") == nil, "Bob no longer talking")

-- 6) Namen mit Sonderzeichen (wie aus C++ escaped) / names with special characters (escaped like in C++)
seen = {}
units.party2 = 'Zo\"e'
run('MVC.Sync({["Zo\\"e"]=1},{})')
check(table.concat(seen, " "):find("VOICE_START:party2") ~= nil, "escaped name resolved")

-- 7) Abschalten setzt alles zurueck (wie NativeUi beim Deaktivieren) / switching off resets everything
run('MVC.Sync({["Me"]=1},{["Me"]=1})')
seen = {}
run('if MVC then MVC.Sync({}, {}) end')
check(table.concat(seen, " "):find("player:VOICE_STOP:player") ~= nil, "reset stops the player icon")
check(UnitIsTalking("Me") == nil and GetVoiceStatus("player") == nil, "reset clears talking and voice")

-- 8) Geraeteliste im Voice-Menue / device list in the voice menu
local refreshed = 0
AudioOptionsVoicePanelInputDeviceDropDown = { RefreshValue = function(self) refreshed = refreshed + 1 end }
DEFAULT = "Standard"
run('MVC.SetDevices({"Mic A","Headset \\"B\\""},{"Speakers"})')
check(Sound_ChatSystem_GetNumInputDrivers() == 3 and Sound_ChatSystem_GetNumOutputDrivers() == 2, "device counts incl. default")
check(Sound_ChatSystem_GetInputDriverNameByIndex(0) == "Standard", "index 0 = default")
check(Sound_ChatSystem_GetInputDriverNameByIndex(2) == 'Headset "B"', "escaped device name")
check(Sound_ChatSystem_GetOutputDriverNameByIndex(1) == "Speakers", "output name")
check(VoiceEnumerateCaptureDevices(1) == "Mic A" and VoiceSelectCaptureDevice("x") == nil, "native select is harmless")
check(refreshed == 1, "dropdown refreshed on change")
run('MVC.SetDevices({"Mic A","Headset \\"B\\""},{"Speakers"})')
check(refreshed == 1, "no refresh without change")

-- 9) Sprecherliste oben links / talker list at the top left
local plates = {}
local talkers = frame({ VOICE_PLATE_START = true, VOICE_PLATE_STOP = true }, "talkers")
talkers.GetScript = function(self, kind)
  return function(self, ev, name, unit) table.insert(plates, ev .. ":" .. tostring(name) .. ":" .. tostring(unit)) end
end
table.insert(frames, talkers)
run('MVC.Sync({["Bob"]=1},{["Bob"]=1},{["Bob"]=1,["Stranger"]=1})')
local p = table.concat(plates, " ")
check(p:find("VOICE_PLATE_START:Bob:party1") ~= nil, "plate start for group member with unit")
check(p:find("VOICE_PLATE_START:Stranger:nil") ~= nil, "plate start for stranger without unit")
plates = {}
run('MVC.Sync({["Bob"]=1},{["Bob"]=1},{["Bob"]=1,["Stranger"]=1})')
check(#plates == 0, "no plate events without change")
run('MVC.Sync({},{},{})')
p = table.concat(plates, " ")
check(p:find("VOICE_PLATE_STOP:Bob:party1") ~= nil and p:find("VOICE_PLATE_STOP:Stranger:nil") ~= nil, "plate stop on reset")

if fails == 0 then print("nativeui_test: all checks passed") end
os.exit(fails == 0 and 0 or 1)
