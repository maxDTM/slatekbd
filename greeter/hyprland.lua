-- Login screen for greetd: noctalia-greeter inside a minimal Hyprland, plus the
-- slatekbd on-screen keyboard (round button left of the greeter's power buttons).
-- Started by slatekbd-greeter-session as the `greeter` user. Nothing else runs here.

hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "auto" })
hl.monitor({ output = "eDP-1", mode = "preferred", position = "auto", scale = 2 })

hl.config({
    general     = { border_size = 0, gaps_in = 0, gaps_out = 0 },
    decoration  = { rounding = 0, shadow = { enabled = false }, blur = { enabled = false } },
    animations  = { enabled = false },
    input       = { kb_layout = "us", touchdevice = { output = "eDP-1" } },
    cursor      = { hide_on_touch = true, inactive_timeout = 5 },
    misc        = { disable_hyprland_logo = true, disable_splash_rendering = true, disable_watchdog_warning = true },
    ecosystem   = { no_update_news = true, no_donation_nag = true },
    debug       = { suppress_errors = true },
})

-- the greeter fills the screen like it does in its own compositor
hl.window_rule({ match = { class = ".*" }, fullscreen = true })
hl.layer_rule({ name = "slatekbd", match = { namespace = "^slatekbd$" }, no_anim = true })

hl.on("hyprland.start", function()
    hl.exec_cmd("/usr/local/bin/slatekbd --greeter --button-offset 3 -c /dev/null")
    -- when the greeter exits (login succeeded), end this session so greetd starts the user's
    hl.exec_cmd("sh -c 'noctalia-greeter; hyprctl dispatch \"hl.dsp.exit()\"'")
end)
