const std = @import("std");

const app_cpp_sources = [_][]const u8{
    "main.cpp",
    "text_renderer.cpp",
    "application.cpp",
    "widget.cpp",
    "button.cpp",
    "titlebar.cpp",
    "panel_holder.cpp",
    "widgetchooser.cpp",
    "editor.cpp",
    "filetree.cpp",
    "filebackend.cpp",
    "sshfilebackend.cpp",
    "tabs.cpp",
    "settings.cpp",
    "codeedit.cpp",
    "linenumbers.cpp",
    "textedit.cpp",
    "brokenstatemenu.cpp",
    "checkbox.cpp",
    "settingsmanager.cpp",
    "languageserverclient.cpp",
    "listbox.cpp",
    "imageview.cpp",
    "compare.cpp",
    "curler.cpp",
    "toast.cpp",
    "chat.cpp",
    "label.cpp",
    "myrect.cpp",
    "lspdebug.cpp",
    "terminal.cpp",
    "terminalwidget.cpp",
    "terminalwidgettabbed.cpp",
    "helpmenu.cpp",
    "scrollbar.cpp",
    "updatechecker.cpp",
    "contextmenu.cpp",
    "scrollnotify.cpp",
    "mathwindow.cpp",
    "asteroids.cpp",
    "graphwindow.cpp",
    "EmojiRenderer.cpp",
    "hexeditor.cpp",
    "MonoString.cpp",
    "statusbar.cpp",
};

const app_c_sources = [_][]const u8{
    "tinyfiledialogs.c",
    "md4c.c",
};

const sioclient_sources = [_][]const u8{
    "src/sio_client.cpp",
    "src/sio_socket.cpp",
    "src/internal/sio_client_impl.cpp",
    "src/internal/sio_packet.cpp",
};

const libgrapheme_sources = [_][]const u8{
    "src/case.c",
    "src/character.c",
    "src/line.c",
    "src/sentence.c",
    "src/utf8.c",
    "src/util.c",
    "src/word.c",
};

const glfw_windows_sources = [_][]const u8{
    "src/context.c",
    "src/egl_context.c",
    "src/init.c",
    "src/input.c",
    "src/monitor.c",
    "src/null_init.c",
    "src/null_joystick.c",
    "src/null_monitor.c",
    "src/null_window.c",
    "src/osmesa_context.c",
    "src/platform.c",
    "src/vulkan.c",
    "src/wgl_context.c",
    "src/win32_init.c",
    "src/win32_joystick.c",
    "src/win32_module.c",
    "src/win32_monitor.c",
    "src/win32_thread.c",
    "src/win32_time.c",
    "src/win32_window.c",
    "src/window.c",
};

const glfw_linux_sources = [_][]const u8{
    "src/context.c",
    "src/egl_context.c",
    "src/glx_context.c",
    "src/init.c",
    "src/input.c",
    "src/linux_joystick.c",
    "src/monitor.c",
    "src/null_init.c",
    "src/null_joystick.c",
    "src/null_monitor.c",
    "src/null_window.c",
    "src/osmesa_context.c",
    "src/platform.c",
    "src/posix_module.c",
    "src/posix_poll.c",
    "src/posix_thread.c",
    "src/posix_time.c",
    "src/vulkan.c",
    "src/window.c",
    "src/x11_init.c",
    "src/x11_monitor.c",
    "src/x11_window.c",
    "src/xkb_unicode.c",
};

const freetype_sources = [_][]const u8{
    "src/autofit/autofit.c",
    "src/base/ftbase.c",
    "src/base/ftbbox.c",
    "src/base/ftbdf.c",
    "src/base/ftbitmap.c",
    "src/base/ftcid.c",
    "src/base/ftfstype.c",
    "src/base/ftgasp.c",
    "src/base/ftglyph.c",
    "src/base/ftgxval.c",
    "src/base/ftinit.c",
    "src/base/ftmm.c",
    "src/base/ftotval.c",
    "src/base/ftpatent.c",
    "src/base/ftpfr.c",
    "src/base/ftstroke.c",
    "src/base/ftsynth.c",
    "src/base/fttype1.c",
    "src/base/ftwinfnt.c",
    "src/base/ftsystem.c",
    "src/base/ftdebug.c",
    "src/bdf/bdf.c",
    "src/cache/ftcache.c",
    "src/cff/cff.c",
    "src/cid/type1cid.c",
    "src/gzip/ftgzip.c",
    "src/lzw/ftlzw.c",
    "src/pcf/pcf.c",
    "src/pfr/pfr.c",
    "src/psaux/psaux.c",
    "src/pshinter/pshinter.c",
    "src/psnames/psnames.c",
    "src/raster/raster.c",
    "src/sdf/sdf.c",
    "src/sfnt/sfnt.c",
    "src/smooth/smooth.c",
    "src/svg/svg.c",
    "src/truetype/truetype.c",
    "src/type1/type1.c",
    "src/type42/type42.c",
    "src/winfonts/winfnt.c",
};

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{ .preferred_optimize_mode = .ReleaseFast });

    const is_windows = target.result.os.tag == .windows;
    const is_linux = target.result.os.tag == .linux;
    if (!is_windows and !is_linux) @panic("CodeWizard currently supports Windows and Linux targets.");

    const module = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    const exe = b.addExecutable(.{
        .name = "CodeWizard",
        .root_module = module,
    });
    if (is_windows) exe.subsystem = .Windows;

    var cpp_flags: std.ArrayList([]const u8) = .empty;
    try cpp_flags.appendSlice(b.allocator, &.{
        "-std=c++17",
        "-DGHOSTTY_STATIC",
        "-Wno-c++11-narrowing",
        "-Wno-literal-conversion",
        "-Wno-shorten-64-to-32",
        "-Wno-macro-redefined",
        "-Wno-inconsistent-missing-override",
    });
    if (is_windows) try cpp_flags.appendSlice(b.allocator, &.{
        "-DNOMINMAX",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-D_WIN32_WINNT=0x0A00",
        "-DNTDDI_VERSION=0x0A000008",
        "-DUNICODE",
        "-D_UNICODE",
        "-DCURL_STATICLIB",
    });
    module.addCSourceFiles(.{
        .files = &app_cpp_sources,
        .flags = cpp_flags.items,
    });
    var c_flags: std.ArrayList([]const u8) = .empty;
    if (is_windows) try c_flags.appendSlice(b.allocator, &.{
        "-DNOMINMAX",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-D_WIN32_WINNT=0x0A00",
        "-DUNICODE",
        "-D_UNICODE",
        "-Wno-macro-redefined",
    });
    module.addCSourceFiles(.{
        .files = &app_c_sources,
        .flags = c_flags.items,
    });
    if (is_windows) exe.addWin32ResourceFile(.{ .file = b.path("resources.rc") });

    const sioclient = b.dependency("sioclient", .{});
    const asio = b.dependency("asio", .{});
    const rapidjson = b.dependency("rapidjson", .{});
    const websocketpp = b.dependency("websocketpp", .{});
    module.addCSourceFiles(.{
        .root = sioclient.path(""),
        .files = &sioclient_sources,
        .flags = &.{
            "-std=c++17",
            "-DASIO_STANDALONE",
            "-DBOOST_DATE_TIME_NO_LIB",
            "-DBOOST_REGEX_NO_LIB",
            "-D_WEBSOCKETPP_CPP11_STL_",
            "-D_WEBSOCKETPP_CPP11_FUNCTIONAL_",
            "-D_WEBSOCKETPP_CPP11_TYPE_TRAITS_",
            "-D_WEBSOCKETPP_CPP11_CHRONO_",
        },
    });

    module.addIncludePath(b.path("."));
    module.addIncludePath(b.path("third_party/syntect_bridge/include"));
    module.addIncludePath(sioclient.path("src"));
    module.addIncludePath(asio.path("asio/include"));
    module.addIncludePath(rapidjson.path("include"));
    module.addIncludePath(websocketpp.path(""));
    const glfw_dep = b.dependency("glfw", .{});
    const freetype_dep = b.dependency("freetype", .{});
    const curl_dep = if (is_windows) b.dependency("curl", .{}) else null;
    const stb_dep = b.dependency("stb", .{});
    module.addIncludePath(glfw_dep.path("include"));
    module.addIncludePath(freetype_dep.path("include"));
    if (curl_dep) |dependency| module.addIncludePath(dependency.path("include"));
    module.addIncludePath(stb_dep.path(""));
    if (is_linux) {
        exe.addLibraryPath(.{ .cwd_relative = "/usr/lib/x86_64-linux-gnu" });
        exe.addLibraryPath(.{ .cwd_relative = "/lib/x86_64-linux-gnu" });
    }

    const ghostty = b.dependency("ghostty", .{
        .target = target,
        .optimize = optimize,
        .@"emit-lib-vt" = true,
        .@"lib-version-string" = "0.1.0-dev",
    });
    exe.linkLibrary(ghostty.artifact("ghostty-vt-static"));
    module.addIncludePath(ghostty.path("include"));

    const glfw = b.addLibrary(.{
        .name = "glfw",
        .linkage = .static,
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    glfw.addIncludePath(glfw_dep.path("include"));
    glfw.addIncludePath(glfw_dep.path("src"));
    glfw.addCSourceFiles(.{
        .root = glfw_dep.path(""),
        .files = if (is_windows) &glfw_windows_sources else &glfw_linux_sources,
        .flags = if (is_windows)
            &.{ "-D_GLFW_WIN32", "-DUNICODE", "-D_UNICODE" }
        else
            &.{"-D_GLFW_X11"},
    });
    exe.linkLibrary(glfw);

    const freetype = b.addLibrary(.{
        .name = "freetype",
        .linkage = .static,
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    freetype.addIncludePath(freetype_dep.path("include"));
    freetype.addCSourceFiles(.{
        .root = freetype_dep.path(""),
        .files = &freetype_sources,
        .flags = if (is_windows)
            &.{ "-DFT2_BUILD_LIBRARY", "-fno-sanitize=undefined" }
        else
            &.{ "-DFT2_BUILD_LIBRARY", "-fno-sanitize=undefined", "-DHAVE_UNISTD_H", "-DHAVE_FCNTL_H" },
    });
    exe.linkLibrary(freetype);

    if (curl_dep) |dependency| {
        const curl = b.addLibrary(.{
            .name = "curl",
            .linkage = .static,
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize,
                .link_libc = true,
            }),
        });
        curl.addIncludePath(dependency.path("include"));
        curl.addIncludePath(dependency.path("lib"));
        curl.addCSourceFiles(.{
            .root = dependency.path("lib"),
            .files = try curlSources(b, dependency),
            .flags = &.{
                "-DBUILDING_LIBCURL",
                "-DCURL_STATICLIB",
                "-DUSE_SCHANNEL",
                "-DUSE_WINDOWS_SSPI",
                "-DUSE_WIN32_IDN",
                "-DCURL_DISABLE_LDAP",
                "-DCURL_DISABLE_LDAPS",
                "-DUNICODE",
                "-D_UNICODE",
                "-Wno-macro-redefined",
            },
        });
        exe.linkLibrary(curl);
    } else {
        exe.linkSystemLibrary("curl");
    }
    const libgrapheme_dep = b.dependency("libgrapheme", .{});
    const generated_grapheme = b.addWriteFiles();
    _ = generated_grapheme.add("src/.keep", "");
    for ([_][]const u8{ "case", "character", "line", "sentence", "word" }) |name| {
        const generator = b.addExecutable(.{
            .name = b.fmt("libgrapheme-gen-{s}", .{name}),
            .root_module = b.createModule(.{
                .target = b.graph.host,
                .optimize = .ReleaseFast,
                .link_libc = true,
            }),
        });
        generator.addCSourceFiles(.{
            .root = libgrapheme_dep.path(""),
            .files = &.{
                b.fmt("gen/{s}.c", .{name}),
                "gen/util.c",
            },
            .flags = &.{
                "-std=c99",
                "-include",
                "build_support/getline_compat.h",
                "-D_DEFAULT_SOURCE",
            },
        });
        const generate = b.addRunArtifact(generator);
        generate.setCwd(libgrapheme_dep.path(""));
        _ = generated_grapheme.addCopyFile(
            generate.captureStdOut(),
            b.fmt("gen/{s}.h", .{name}),
        );
    }
    const libgrapheme = b.addLibrary(.{
        .name = "grapheme",
        .linkage = .static,
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    libgrapheme.addIncludePath(libgrapheme_dep.path(""));
    libgrapheme.addIncludePath(generated_grapheme.getDirectory().path(b, "src"));
    libgrapheme.addCSourceFiles(.{
        .root = libgrapheme_dep.path(""),
        .files = &libgrapheme_sources,
        .flags = &.{"-std=c99"},
    });
    exe.linkLibrary(libgrapheme);

    const rust_target = if (is_windows) "x86_64-pc-windows-gnu" else "x86_64-unknown-linux-gnu";
    const rust_target_dir = if (is_windows) "target/zig-windows" else "target/zig-linux";
    const cargo = b.addSystemCommand(&.{
        "cargo",
        "build",
        "--release",
        "--target",
        rust_target,
        "--target-dir",
        rust_target_dir,
    });
    cargo.setCwd(b.path("third_party/syntect_bridge"));
    exe.step.dependOn(&cargo.step);
    exe.addObjectFile(.{ .cwd_relative = b.pathFromRoot(b.fmt(
        "third_party/syntect_bridge/{s}/{s}/release/libsyntect_bridge.a",
        .{ rust_target_dir, rust_target },
    )) });

    if (is_windows) {
        for ([_][]const u8{
            "opengl32", "ole32",  "shell32",  "dwmapi",  "userenv",  "ntdll",
            "ws2_32",   "bcrypt", "crypt32",  "d2d1",    "dwrite",   "windowscodecs",
            "comdlg32", "gdi32",  "advapi32", "secur32", "normaliz", "iphlpapi",
        }) |library| exe.linkSystemLibrary(library);
    } else {
        for ([_][]const u8{
            "GL",      "X11", "Xrandr", "Xinerama", "Xcursor", "Xi",
            "pthread", "dl",  "m",      "ssl",      "crypto",
        }) |library| exe.linkSystemLibrary(library);
    }

    const install_exe = b.addInstallArtifact(exe, .{
        .dest_dir = .{ .override = .prefix },
        .pdb_dir = .disabled,
    });
    b.getInstallStep().dependOn(&install_exe.step);
    installRuntimeFiles(b);

    const run = b.addRunArtifact(exe);
    run.step.dependOn(b.getInstallStep());
    if (b.args) |args| run.addArgs(args);
    b.step("run", "Build and run CodeWizard").dependOn(&run.step);
}

fn installRuntimeFiles(b: *std.Build) void {
    b.installFile("app.png", "app.png");
    b.installFile("fileIcon.png", "fileIcon.png");
    b.installFile("folderIcon.png", "folderIcon.png");
    b.installFile("build_needs/splashscreen.png", "splashscreen.png");
    b.installDirectory(.{
        .source_dir = b.path("cascadia"),
        .install_dir = .prefix,
        .install_subdir = "cascadia",
    });
}

fn curlSources(b: *std.Build, dependency: *std.Build.Dependency) ![]const []const u8 {
    const makefile_path = dependency.path("lib/Makefile.inc").getPath(b);
    const contents = try std.fs.cwd().readFileAlloc(b.allocator, makefile_path, 1024 * 1024);
    var result: std.ArrayList([]const u8) = .empty;
    var seen: std.StringHashMapUnmanaged(void) = .empty;

    var tokens = std.mem.tokenizeAny(u8, contents, " \t\r\n\\");
    while (tokens.next()) |token| {
        if (!std.mem.endsWith(u8, token, ".c")) continue;
        if (std.mem.indexOfScalar(u8, token, '$') != null) continue;
        const entry = try b.allocator.dupe(u8, token);
        const added = try seen.getOrPut(b.allocator, entry);
        if (!added.found_existing) try result.append(b.allocator, entry);
    }
    return result.toOwnedSlice(b.allocator);
}
