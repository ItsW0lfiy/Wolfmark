set_project("Wolfmark")
set_version("0.2.0-dev.1")
set_allowedmodes("debug", "release")
set_defaultmode("debug")

target("wolfmark")
    set_kind("phony")
    on_build(function ()
        local args = {"build", "--locked"}
        if is_mode("release") then table.insert(args, "--release") end
        os.execv("cargo", args)
    end)
    on_run(function ()
        import("core.base.option")
        local args = {"run", "--locked", "--bin", "wolfmark"}
        if is_mode("release") then table.insert(args, "--release") end
        local runargs = table.wrap(option.get("arguments"))
        if #runargs > 0 then
            table.insert(args, "--")
            table.join2(args, runargs)
        end
        os.execv("cargo", args)
    end)
    on_clean(function () os.execv("cargo", {"clean"}) end)
target_end()

task("test")
    on_run(function ()
        if is_host("windows") then
            os.execv("pwsh", {"-NoProfile", "-File", "scripts/run_background_windows.ps1", "-Action", "test"})
            return
        end
        os.execv("cargo", {"test", "--package", "wolfmark", "--locked", "--no-fail-fast", "--", "--test-threads=1"})
    end)
    set_menu {usage = "xmake test", description = "Run Rust and serialized native Qt regressions"}
task_end()

task("check")
    on_run(function ()
        os.execv("cargo", {"fmt", "--all", "--check"})
        local scope = is_host("windows") and {"--workspace"} or {"--package", "wolfmark"}
        os.execv("cargo", table.join({"check"}, scope, {"--locked"}))
        os.execv("cargo", table.join({"clippy"}, scope, {"--all-targets", "--all-features", "--locked", "--", "-D", "warnings"}))
    end)
    set_menu {usage = "xmake check", description = "Formatting, type checking, and warnings"}
task_end()

task("package")
    on_run(function ()
        assert(is_host("windows"), "Windows packaging requires Windows; Linux packaging is not yet implemented")
        os.execv("pwsh", {"-NoProfile", "-File", "scripts/run_background_windows.ps1", "-Action", "package"})
    end)
    set_menu {usage = "xmake package", description = "Build Windows installer, portable ZIP, and checksums"}
task_end()
