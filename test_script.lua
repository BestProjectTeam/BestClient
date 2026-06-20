script.name        = "Test Script"
script.description = "Tests ui.* widgets and storage"
script.version     = "1.0"
script.author      = "BestClient"

local show_hud = storage.get("show_hud", true)
local hud_size = storage.get("hud_size", 14.0)
local hud_x    = storage.get("hud_x", 10.0)
local hud_y    = storage.get("hud_y", 10.0)

script.on_settings = function()
    ui.text("HUD Settings:")
    ui.separator()
    local new_show = ui.checkbox("Show speed HUD", show_hud)
    if new_show ~= show_hud then
        show_hud = new_show
        storage.set("show_hud", show_hud)
    end
    local new_size = ui.slider("Font size", hud_size, 8, 28)
    if new_size ~= hud_size then
        hud_size = new_size
        storage.set("hud_size", hud_size)
    end
    local new_x = ui.slider("X position", hud_x, 0, 400)
    if new_x ~= hud_x then
        hud_x = new_x
        storage.set("hud_x", hud_x)
    end
    local new_y = ui.slider("Y position", hud_y, 0, 300)
    if new_y ~= hud_y then
        hud_y = new_y
        storage.set("hud_y", hud_y)
    end
    ui.separator()
    if ui.button("Reset defaults") then
        show_hud = true
        hud_size = 14.0
        hud_x    = 10.0
        hud_y    = 10.0
        storage.set("show_hud", show_hud)
        storage.set("hud_size", hud_size)
        storage.set("hud_x", hud_x)
        storage.set("hud_y", hud_y)
    end
end

script.on_render = function()
    if not show_hud then return end
    if game.state() ~= "online" then return end
    local vel = player.vel()
    if not vel then return end
    local speed = math.sqrt(vel.x * vel.x + vel.y * vel.y)
    render.text(hud_x, hud_y, hud_size, string.format("Speed: %.2f", speed), {r=1,g=1,b=1,a=1})
end
