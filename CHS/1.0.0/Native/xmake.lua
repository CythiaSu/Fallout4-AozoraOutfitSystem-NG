-- set minimum xmake version
set_xmakever("3.0.0")

-- set project constants
set_project("OutfitManager")
set_version("1.0.0")
set_arch("x64")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

-- add rules
add_rules("mode.debug", "mode.releasedbg")

-- use the multiruntime CommonLibF4 baseline
includes("../commonlibf4-frakkin64")

-- define target
target("OutfitManager", function()
    -- use commonlibf4 plugin rule
    add_rules("commonlibf4.plugin", {
        name = "OutfitManager",
        author = "OutfitManager Author",
        plugin_template = "commonlibf4-plugin.cpp.in"
    })

    -- add source files
    add_files("src/**.cpp")

    -- add include directories
    add_includedirs("src")
end)
